#ifdef MCP_HTTP_TRANSPORT

#include "mcp/transport/streamable_http_client.hpp"
#include "mcp/service/service_role.hpp"

#include <sstream>
#include <stdexcept>
#include <string_view>

#include <spdlog/spdlog.h>

namespace mcp {

// =============================================================================
// URL parsing
// =============================================================================

ParsedUrl parse_url(const std::string& url) {
    ParsedUrl result;

    std::string_view sv(url);

    // Determine scheme
    if (sv.starts_with("https://")) {
        result.is_https = true;
        sv.remove_prefix(8);
    } else if (sv.starts_with("http://")) {
        result.is_https = false;
        sv.remove_prefix(7);
    } else {
        throw std::invalid_argument("URL must start with http:// or https://");
    }

    // Find the path separator
    auto path_pos = sv.find('/');
    std::string_view authority;
    if (path_pos == std::string_view::npos) {
        authority = sv;
        result.target = "/";
    } else {
        authority = sv.substr(0, path_pos);
        result.target = std::string(sv.substr(path_pos));
    }

    // Split host:port
    // Handle IPv6 addresses in brackets: [::1]:8080
    if (!authority.empty() && authority.front() == '[') {
        auto bracket_end = authority.find(']');
        if (bracket_end == std::string_view::npos) {
            throw std::invalid_argument("Malformed IPv6 address in URL");
        }
        result.host = std::string(authority.substr(1, bracket_end - 1));
        if (bracket_end + 1 < authority.size() && authority[bracket_end + 1] == ':') {
            result.port = std::string(authority.substr(bracket_end + 2));
        } else {
            result.port = result.is_https ? "443" : "80";
        }
    } else {
        auto colon_pos = authority.find(':');
        if (colon_pos == std::string_view::npos) {
            result.host = std::string(authority);
            result.port = result.is_https ? "443" : "80";
        } else {
            result.host = std::string(authority.substr(0, colon_pos));
            result.port = std::string(authority.substr(colon_pos + 1));
        }
    }

    if (result.target.empty()) {
        result.target = "/";
    }

    return result;
}

// =============================================================================
// SSE event parsing
// =============================================================================

std::vector<SseEvent> parse_sse_events(const std::string& body) {
    std::vector<SseEvent> events;
    SseEvent current;
    bool has_data = false;

    std::istringstream stream(body);
    std::string line;

    while (std::getline(stream, line)) {
        // Remove trailing \r if present (handles \r\n line endings)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            // Empty line: dispatch current event if we have data
            if (has_data) {
                // Remove trailing newline from data if present
                if (!current.data.empty() && current.data.back() == '\n') {
                    current.data.pop_back();
                }
                events.push_back(std::move(current));
                current = SseEvent{};
                has_data = false;
            }
            continue;
        }

        // Comment lines start with ':'
        if (line.front() == ':') {
            continue;
        }

        // Find the field name and value
        auto colon_pos = line.find(':');
        std::string field;
        std::string value;

        if (colon_pos == std::string::npos) {
            // Field with no value
            field = line;
        } else {
            field = line.substr(0, colon_pos);
            // Skip the colon and optional single leading space
            size_t value_start = colon_pos + 1;
            if (value_start < line.size() && line[value_start] == ' ') {
                value_start++;
            }
            value = line.substr(value_start);
        }

        if (field == "data") {
            if (has_data) {
                current.data += '\n';
            }
            current.data += value;
            has_data = true;
        } else if (field == "id") {
            current.id = value;
        } else if (field == "event") {
            current.event = value;
        } else if (field == "retry") {
            try {
                current.retry_ms = std::stoi(value);
            } catch (...) {
                // Ignore invalid retry values per SSE spec
            }
        }
        // Unknown fields are ignored per SSE spec
    }

    // If the stream ends without a trailing blank line, dispatch any pending event
    if (has_data) {
        if (!current.data.empty() && current.data.back() == '\n') {
            current.data.pop_back();
        }
        events.push_back(std::move(current));
    }

    return events;
}

// =============================================================================
// StreamableHttpClientTransport implementation
// =============================================================================

template <typename R>
StreamableHttpClientTransport<R>::StreamableHttpClientTransport(
    asio::any_io_executor executor,
    StreamableHttpClientConfig config)
    : executor_(std::move(executor))
    , config_(config)
    , parsed_url_(parse_url(config.url))
    , queue_signal_(std::make_shared<asio::steady_timer>(executor_))
{
    // Set the timer to a far-future expiry so waiters block until signalled
    queue_signal_->expires_at(asio::steady_timer::time_point::max());
    spdlog::debug(
        "StreamableHttpClientTransport: connecting to {}:{}{}", parsed_url_.host,
        parsed_url_.port, parsed_url_.target);
}

template <typename R>
StreamableHttpClientTransport<R>::~StreamableHttpClientTransport() {
    // Cancel any pending waiters
    if (queue_signal_) {
        queue_signal_->cancel();
    }
}

template <typename R>
template <typename Body>
void StreamableHttpClientTransport<R>::set_common_headers(http::request<Body>& req) {
    req.set(http::field::host, parsed_url_.host);
    req.set(http::field::accept, "application/json, text/event-stream");
    req.set(http::field::content_type, "application/json");
    req.set(http::field::user_agent, "mcp-cpp/0.1.0");

    if (session_id_.has_value()) {
        req.set("Mcp-Session-Id", *session_id_);
    }

    if (config_.auth_header.has_value()) {
        req.set(http::field::authorization, *config_.auth_header);
    }

    if (last_event_id_.has_value()) {
        req.set("Last-Event-Id", *last_event_id_);
    }
}

template <typename R>
asio::awaitable<http::response<http::string_body>>
StreamableHttpClientTransport<R>::do_post(const std::string& body) {
    // Resolve the host
    tcp::resolver resolver(executor_);
    auto endpoints = co_await resolver.async_resolve(
        parsed_url_.host, parsed_url_.port, asio::use_awaitable);

    // Connect
    beast::tcp_stream stream(executor_);
    stream.expires_after(std::chrono::seconds(30));
    co_await stream.async_connect(endpoints, asio::use_awaitable);

    // Build request
    http::request<http::string_body> req{http::verb::post, parsed_url_.target, 11};
    set_common_headers(req);
    req.body() = body;
    req.prepare_payload();

    // Send
    stream.expires_after(std::chrono::seconds(30));
    co_await http::async_write(stream, req, asio::use_awaitable);

    // Receive response
    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    stream.expires_after(std::chrono::seconds(60));
    co_await http::async_read(stream, buffer, res, asio::use_awaitable);

    // Gracefully close the connection
    boost::system::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    // Ignore shutdown errors (connection may already be closed)

    co_return res;
}

template <typename R>
asio::awaitable<void> StreamableHttpClientTransport<R>::do_delete() {
    try {
        tcp::resolver resolver(executor_);
        auto endpoints = co_await resolver.async_resolve(
            parsed_url_.host, parsed_url_.port, asio::use_awaitable);

        beast::tcp_stream stream(executor_);
        stream.expires_after(std::chrono::seconds(10));
        co_await stream.async_connect(endpoints, asio::use_awaitable);

        http::request<http::empty_body> req{
            http::verb::delete_, parsed_url_.target, 11};
        req.set(http::field::host, parsed_url_.host);
        req.set(http::field::user_agent, "mcp-cpp/0.1.0");
        if (session_id_.has_value()) {
            req.set("Mcp-Session-Id", *session_id_);
        }
        if (config_.auth_header.has_value()) {
            req.set(http::field::authorization, *config_.auth_header);
        }
        req.prepare_payload();

        stream.expires_after(std::chrono::seconds(10));
        co_await http::async_write(stream, req, asio::use_awaitable);

        // Read and discard response
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        stream.expires_after(std::chrono::seconds(10));
        co_await http::async_read(stream, buffer, res, asio::use_awaitable);

        spdlog::debug(
            "StreamableHttpClientTransport: DELETE session response: {}",
            static_cast<int>(res.result()));

        boost::system::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    } catch (const std::exception& e) {
        spdlog::warn(
            "StreamableHttpClientTransport: failed to send DELETE: {}", e.what());
    }

    co_return;
}

template <typename R>
void StreamableHttpClientTransport<R>::enqueue_message(RxJsonRpcMessage<R> msg) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        receive_queue_.push(std::move(msg));
    }
    // Wake up any waiting receive() call
    queue_signal_->cancel();
}

template <typename R>
void StreamableHttpClientTransport<R>::enqueue_sse_messages(const std::string& body) {
    auto events = parse_sse_events(body);
    for (auto& event : events) {
        // Track the last event ID for reconnection
        if (event.id.has_value()) {
            last_event_id_ = event.id;
        }

        // Only process "message" events or events with no explicit type
        // (the default SSE event type is "message")
        if (event.event.has_value() && *event.event != "message") {
            spdlog::debug(
                "StreamableHttpClientTransport: ignoring SSE event type: {}",
                *event.event);
            continue;
        }

        if (event.data.empty()) {
            continue;
        }

        try {
            auto j = json::parse(event.data);
            auto msg = j.get<RxJsonRpcMessage<R>>();
            enqueue_message(std::move(msg));
        } catch (const std::exception& e) {
            spdlog::error(
                "StreamableHttpClientTransport: failed to parse SSE JSON data: {}",
                e.what());
        }
    }
}

template <typename R>
asio::awaitable<void> StreamableHttpClientTransport<R>::send(TxJsonRpcMessage<R> msg) {
    if (closed_) {
        throw std::runtime_error("Transport is closed");
    }

    // Serialize the message to JSON
    json j = msg;
    std::string body = j.dump();

    spdlog::debug("StreamableHttpClientTransport: POST {}", body);

    // Perform the HTTP POST
    auto res = co_await do_post(body);

    auto status = res.result();
    spdlog::debug(
        "StreamableHttpClientTransport: response status={}",
        static_cast<int>(status));

    // Check for session ID in response headers
    auto session_it = res.find("Mcp-Session-Id");
    if (session_it != res.end()) {
        std::string new_session_id(session_it->value());
        if (!session_id_.has_value() || *session_id_ != new_session_id) {
            spdlog::info(
                "StreamableHttpClientTransport: session established: {}",
                new_session_id);
            session_id_ = std::move(new_session_id);
        }
    }

    // Handle error responses
    if (status != http::status::ok && status != http::status::accepted
        && status != http::status::no_content)
    {
        spdlog::error(
            "StreamableHttpClientTransport: server returned HTTP {}: {}",
            static_cast<int>(status), res.body());

        // If the message was a request (has an id), synthesize an error response
        // so the caller gets notified
        if (msg.is_request()) {
            auto id = msg.as_request().id;
            ErrorData err(
                ErrorCode::INTERNAL_ERROR,
                "HTTP error: " + std::to_string(static_cast<int>(status)));
            enqueue_message(RxJsonRpcMessage<R>(typename RxJsonRpcMessage<R>::Error{
                "2.0", std::move(id), std::move(err)}));
        }
        co_return;
    }

    // 202 Accepted or 204 No Content: server accepted but no body to parse
    if (status == http::status::accepted || status == http::status::no_content) {
        co_return;
    }

    // Determine content type
    auto content_type_it = res.find(http::field::content_type);
    std::string content_type;
    if (content_type_it != res.end()) {
        content_type = std::string(content_type_it->value());
    }

    if (content_type.find("text/event-stream") != std::string::npos) {
        // SSE response: parse events and enqueue messages
        enqueue_sse_messages(res.body());
    } else if (content_type.find("application/json") != std::string::npos) {
        // Direct JSON response: parse and enqueue a single message
        try {
            auto response_json = json::parse(res.body());

            // The response body could be a single JSON-RPC message or an array
            // of JSON-RPC messages (batch response).
            if (response_json.is_array()) {
                for (auto& item : response_json) {
                    auto rx_msg = item.template get<RxJsonRpcMessage<R>>();
                    enqueue_message(std::move(rx_msg));
                }
            } else {
                auto rx_msg = response_json.template get<RxJsonRpcMessage<R>>();
                enqueue_message(std::move(rx_msg));
            }
        } catch (const std::exception& e) {
            spdlog::error(
                "StreamableHttpClientTransport: failed to parse JSON response: {}",
                e.what());
        }
    } else if (!res.body().empty()) {
        // Unknown content type but non-empty body - try to parse as JSON
        spdlog::warn(
            "StreamableHttpClientTransport: unexpected content-type '{}', "
            "attempting JSON parse",
            content_type);
        try {
            auto response_json = json::parse(res.body());
            auto rx_msg = response_json.template get<RxJsonRpcMessage<R>>();
            enqueue_message(std::move(rx_msg));
        } catch (const std::exception& e) {
            spdlog::error(
                "StreamableHttpClientTransport: failed to parse response body: {}",
                e.what());
        }
    }

    co_return;
}

template <typename R>
asio::awaitable<std::optional<RxJsonRpcMessage<R>>>
StreamableHttpClientTransport<R>::receive() {
    while (true) {
        // Check if we already have messages queued
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (!receive_queue_.empty()) {
                auto msg = std::move(receive_queue_.front());
                receive_queue_.pop();
                co_return std::move(msg);
            }
            if (closed_) {
                co_return std::nullopt;
            }
        }

        // Wait for a signal that new messages are available.
        // The timer is set to max expiry; cancel() is used to wake us up.
        boost::system::error_code ec;
        co_await queue_signal_->async_wait(
            asio::redirect_error(asio::use_awaitable, ec));

        // Reset the timer for the next wait cycle
        queue_signal_->expires_at(asio::steady_timer::time_point::max());

        // Re-check the queue (the cancel may have been from enqueue or close)
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (!receive_queue_.empty()) {
                auto msg = std::move(receive_queue_.front());
                receive_queue_.pop();
                co_return std::move(msg);
            }
            if (closed_) {
                co_return std::nullopt;
            }
        }
    }
}

template <typename R>
asio::awaitable<void> StreamableHttpClientTransport<R>::close() {
    if (closed_) {
        co_return;
    }

    spdlog::debug("StreamableHttpClientTransport: closing transport");
    closed_ = true;

    // Send DELETE to terminate the session if one was established
    if (session_id_.has_value()) {
        co_await do_delete();
    }

    // Wake up any waiting receive() call
    queue_signal_->cancel();

    co_return;
}

// =============================================================================
// Explicit template instantiations
// =============================================================================

template class StreamableHttpClientTransport<RoleClient>;
template class StreamableHttpClientTransport<RoleServer>;

} // namespace mcp

#endif // MCP_HTTP_TRANSPORT
