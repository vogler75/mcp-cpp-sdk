#ifdef MCP_HTTP_TRANSPORT

#include "mcp/transport/streamable_http_server.hpp"

#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

#include <spdlog/spdlog.h>

namespace mcp {

// =============================================================================
// ServerSession
// =============================================================================

ServerSession::ServerSession(asio::any_io_executor executor, std::string id)
    : session_id(std::move(id))
    , incoming_signal(std::make_shared<asio::steady_timer>(executor))
    , outgoing_signal(std::make_shared<asio::steady_timer>(executor)) {
    // Set timers to expire far in the future so they block until signalled
    incoming_signal->expires_at(asio::steady_timer::time_point::max());
    outgoing_signal->expires_at(asio::steady_timer::time_point::max());
}

void ServerSession::push_incoming(json msg) {
    {
        std::lock_guard<std::mutex> lock(incoming_mutex);
        incoming_messages.push(std::move(msg));
    }
    // Cancel the timer to wake up any waiting receive() call
    incoming_signal->cancel();
}

std::optional<json> ServerSession::try_pop_incoming() {
    std::lock_guard<std::mutex> lock(incoming_mutex);
    if (incoming_messages.empty()) {
        return std::nullopt;
    }
    auto msg = std::move(incoming_messages.front());
    incoming_messages.pop();
    return msg;
}

void ServerSession::push_outgoing(json msg) {
    {
        std::lock_guard<std::mutex> lock(outgoing_mutex);
        outgoing_messages.push(std::move(msg));
    }
    // Cancel the timer to wake up any waiting SSE sender
    outgoing_signal->cancel();
}

std::optional<json> ServerSession::try_pop_outgoing() {
    std::lock_guard<std::mutex> lock(outgoing_mutex);
    if (outgoing_messages.empty()) {
        return std::nullopt;
    }
    auto msg = std::move(outgoing_messages.front());
    outgoing_messages.pop();
    return msg;
}

// =============================================================================
// SSE formatting helpers
// =============================================================================

std::string format_sse_event(
    const json& message,
    const std::optional<std::string>& event_id,
    const std::optional<int>& retry_ms) {
    std::string result;
    if (event_id.has_value()) {
        result += "id: " + *event_id + "\n";
    }
    if (retry_ms.has_value()) {
        result += "retry: " + std::to_string(*retry_ms) + "\n";
    }
    result += "data: " + message.dump() + "\n";
    result += "\n";
    return result;
}

std::string format_sse_keepalive() {
    return ": keepalive\n\n";
}

std::string format_sse_priming(int64_t event_id, int retry_ms) {
    std::string result;
    result += "id: " + std::to_string(event_id) + "\n";
    result += "retry: " + std::to_string(retry_ms) + "\n";
    result += "data: \n";
    result += "\n";
    return result;
}

std::string generate_session_id() {
    static thread_local std::mt19937 rng(
        static_cast<unsigned>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()));

    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    auto rand32 = [&]() { return dist(rng); };

    // Generate random bytes for UUID v4 format: 8-4-4-4-12
    uint32_t a = rand32();
    uint16_t b = static_cast<uint16_t>(rand32() & 0xFFFF);
    // Version 4: high nibble of third group is 0x4
    uint16_t c = static_cast<uint16_t>((rand32() & 0x0FFF) | 0x4000);
    // Variant 1: high two bits of fourth group are 0b10
    uint16_t d = static_cast<uint16_t>((rand32() & 0x3FFF) | 0x8000);
    uint32_t e1 = rand32();
    uint16_t e2 = static_cast<uint16_t>(rand32() & 0xFFFF);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << std::setw(8) << a << '-';
    oss << std::setw(4) << b << '-';
    oss << std::setw(4) << c << '-';
    oss << std::setw(4) << d << '-';
    oss << std::setw(8) << e1;
    oss << std::setw(4) << e2;
    return oss.str();
}

// =============================================================================
// StreamableHttpServerTransport
// =============================================================================

StreamableHttpServerTransport::StreamableHttpServerTransport(
    asio::any_io_executor executor,
    StreamableHttpServerConfig config)
    : executor_(std::move(executor)), config_(std::move(config)) {}

StreamableHttpServerTransport::~StreamableHttpServerTransport() {
    // Best-effort cleanup; close() should be called explicitly via co_await.
    if (acceptor_ && acceptor_->is_open()) {
        boost::system::error_code ec;
        acceptor_->close(ec);
    }
}

asio::awaitable<void> StreamableHttpServerTransport::start() {
    auto endpoint = tcp::endpoint(
        asio::ip::make_address(config_.host), config_.port);

    acceptor_ = std::make_shared<tcp::acceptor>(executor_);
    acceptor_->open(endpoint.protocol());
    acceptor_->set_option(asio::socket_base::reuse_address(true));
    acceptor_->bind(endpoint);
    acceptor_->listen(asio::socket_base::max_listen_connections);

    actual_port_ = acceptor_->local_endpoint().port();

    spdlog::info(
        "StreamableHttpServerTransport: listening on {}:{} (path: {})",
        config_.host, actual_port_, config_.path);

    // Spawn the accept loop as a background coroutine
    asio::co_spawn(executor_, accept_loop(), asio::detached);

    co_return;
}

uint16_t StreamableHttpServerTransport::port() const {
    return actual_port_;
}

// =============================================================================
// Transport interface: send / receive / close
// =============================================================================

asio::awaitable<void> StreamableHttpServerTransport::send(
    TxJsonRpcMessage<RoleServer> msg) {
    if (closed_) co_return;

    json j = msg;

    std::shared_ptr<ServerSession> session;
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        session = active_session_;
    }

    if (!session) {
        spdlog::warn("StreamableHttpServerTransport::send: no active session");
        co_return;
    }

    spdlog::debug(
        "StreamableHttpServerTransport::send: queuing outgoing message for session {}",
        session->session_id);

    session->push_outgoing(std::move(j));
    co_return;
}

asio::awaitable<std::optional<RxJsonRpcMessage<RoleServer>>>
StreamableHttpServerTransport::receive() {
    while (!closed_) {
        std::shared_ptr<ServerSession> session;
        {
            std::lock_guard<std::mutex> lock(session_mutex_);
            session = active_session_;
        }

        if (!session) {
            // No session yet; wait briefly and retry
            auto timer = asio::steady_timer(executor_, std::chrono::milliseconds(50));
            boost::system::error_code ec;
            co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
            continue;
        }

        if (session->closed) {
            co_return std::nullopt;
        }

        // Check for a queued message first
        auto msg = session->try_pop_incoming();
        if (msg.has_value()) {
            try {
                co_return msg->get<RxJsonRpcMessage<RoleServer>>();
            } catch (const std::exception& e) {
                spdlog::error(
                    "StreamableHttpServerTransport::receive: "
                    "failed to parse incoming message: {}",
                    e.what());
                continue;
            }
        }

        // Wait for a signal that a new message has arrived
        boost::system::error_code ec;
        co_await session->incoming_signal->async_wait(
            asio::redirect_error(asio::use_awaitable, ec));
        // Timer was cancelled (i.e. signalled) or expired; loop back to check
        // Reset the timer so we can wait again
        session->incoming_signal->expires_at(asio::steady_timer::time_point::max());
    }

    co_return std::nullopt;
}

asio::awaitable<void> StreamableHttpServerTransport::close() {
    closed_ = true;

    // Close the active session
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        if (active_session_) {
            active_session_->closed = true;
            active_session_->incoming_signal->cancel();
            active_session_->outgoing_signal->cancel();
        }
    }

    // Close the acceptor
    if (acceptor_ && acceptor_->is_open()) {
        boost::system::error_code ec;
        acceptor_->close(ec);
        if (ec) {
            spdlog::warn(
                "StreamableHttpServerTransport::close: error closing acceptor: {}",
                ec.message());
        }
    }

    spdlog::info("StreamableHttpServerTransport: closed");
    co_return;
}

// =============================================================================
// Session management
// =============================================================================

std::shared_ptr<ServerSession> StreamableHttpServerTransport::get_session(
    const std::string& session_id) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    if (active_session_ && active_session_->session_id == session_id) {
        return active_session_;
    }
    return nullptr;
}

std::shared_ptr<ServerSession> StreamableHttpServerTransport::create_session() {
    auto id = generate_session_id();
    auto session = std::make_shared<ServerSession>(executor_, id);
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        active_session_ = session;
    }
    spdlog::info(
        "StreamableHttpServerTransport: created session {}", id);
    return session;
}

// =============================================================================
// HTTP accept loop
// =============================================================================

asio::awaitable<void> StreamableHttpServerTransport::accept_loop() {
    while (!closed_) {
        boost::system::error_code ec;
        tcp::socket socket(executor_);
        co_await acceptor_->async_accept(
            socket, asio::redirect_error(asio::use_awaitable, ec));

        if (ec) {
            if (closed_) break;
            spdlog::warn(
                "StreamableHttpServerTransport: accept error: {}",
                ec.message());
            continue;
        }

        // Handle each connection in its own detached coroutine
        asio::co_spawn(
            executor_,
            handle_connection(std::move(socket)),
            asio::detached);
    }
}

// =============================================================================
// HTTP connection handler
// =============================================================================

asio::awaitable<void> StreamableHttpServerTransport::handle_connection(
    tcp::socket socket) {
    beast::flat_buffer buffer;

    try {
        // Read the HTTP request
        http::request<http::string_body> req;
        co_await http::async_read(
            socket, buffer, req, asio::use_awaitable);

        // Validate request target matches configured path
        auto target = std::string(req.target());
        // Strip query string for path matching
        auto query_pos = target.find('?');
        auto path = (query_pos != std::string::npos)
            ? target.substr(0, query_pos) : target;

        if (path != config_.path) {
            auto resp = error_response(
                http::status::not_found,
                "Not found: " + path);
            resp.set(http::field::connection, "close");
            co_await http::async_write(
                socket, resp, asio::use_awaitable);
            co_return;
        }

        // Route based on HTTP method
        if (req.method() == http::verb::post) {
            auto resp = co_await handle_post(req);
            co_await http::async_write(
                socket, resp, asio::use_awaitable);
        } else if (req.method() == http::verb::get) {
            co_await handle_get(socket, req);
        } else if (req.method() == http::verb::delete_) {
            auto resp = handle_delete(req);
            co_await http::async_write(
                socket, resp, asio::use_awaitable);
        } else {
            auto resp = error_response(
                http::status::method_not_allowed,
                "Method not allowed");
            resp.set(http::field::connection, "close");
            co_await http::async_write(
                socket, resp, asio::use_awaitable);
        }
    } catch (const boost::system::system_error& e) {
        if (e.code() != beast::errc::not_connected
            && e.code() != asio::error::eof
            && e.code() != asio::error::connection_reset
            && e.code() != asio::error::operation_aborted) {
            spdlog::warn(
                "StreamableHttpServerTransport: connection error: {}",
                e.what());
        }
    } catch (const std::exception& e) {
        spdlog::warn(
            "StreamableHttpServerTransport: connection exception: {}",
            e.what());
    }

    // Close the socket gracefully
    boost::system::error_code ec;
    socket.shutdown(tcp::socket::shutdown_both, ec);
}

// =============================================================================
// POST handler
// =============================================================================

asio::awaitable<http::response<http::string_body>>
StreamableHttpServerTransport::handle_post(
    http::request<http::string_body>& req) {
    // Validate Content-Type
    auto content_type = std::string(req[http::field::content_type]);
    if (content_type.find("application/json") == std::string::npos) {
        co_return error_response(
            http::status::unsupported_media_type,
            "Content-Type must be application/json");
    }

    // Validate Accept header includes required media types
    auto accept = std::string(req[http::field::accept]);
    if (accept.find("application/json") == std::string::npos
        && accept.find("text/event-stream") == std::string::npos
        && accept.find("*/*") == std::string::npos) {
        co_return error_response(
            http::status::not_acceptable,
            "Accept must include application/json or text/event-stream");
    }

    // Parse the JSON body
    json body;
    try {
        body = json::parse(req.body());
    } catch (const std::exception& e) {
        co_return error_response(
            http::status::bad_request,
            std::string("Invalid JSON: ") + e.what());
    }

    // Check for Mcp-Session-Id header
    auto session_id_it = req.find("Mcp-Session-Id");
    bool has_session_id = (session_id_it != req.end());

    if (!has_session_id) {
        // No session ID: this must be an initialize request.
        // Verify that it is a JSON-RPC request with method "initialize".
        bool is_initialize = false;
        if (body.is_object()
            && body.contains("method")
            && body["method"].is_string()) {
            is_initialize = (body["method"].get<std::string>() == "initialize");
        }

        if (config_.stateful_mode && !is_initialize) {
            co_return error_response(
                http::status::bad_request,
                "First request must be an initialize request");
        }

        // Create a new session
        auto session = create_session();

        // Push the message to the session's incoming queue
        session->push_incoming(body);

        // For the initialize request, we respond with SSE so the server can
        // stream back the initialize result. Wait for the response from the
        // service layer (which arrives via send() -> outgoing queue).
        // We poll the outgoing queue for the response.
        json response_msg;
        bool got_response = false;

        // Wait for the outgoing response with a timeout
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (std::chrono::steady_clock::now() < deadline) {
            auto out = session->try_pop_outgoing();
            if (out.has_value()) {
                response_msg = std::move(*out);
                got_response = true;
                break;
            }
            if (session->closed) break;

            // Wait for signal
            boost::system::error_code ec;
            session->outgoing_signal->expires_after(std::chrono::milliseconds(100));
            co_await session->outgoing_signal->async_wait(
                asio::redirect_error(asio::use_awaitable, ec));
        }

        if (!got_response) {
            co_return error_response(
                http::status::gateway_timeout,
                "Timed out waiting for initialize response");
        }

        // Build SSE response containing the initialize result
        auto event_id = std::to_string(session->next_event_id++);
        auto sse_body = format_sse_event(response_msg, event_id);

        http::response<http::string_body> resp{
            http::status::ok, req.version()};
        resp.set(http::field::content_type, "text/event-stream");
        resp.set(http::field::cache_control, "no-cache");
        resp.set(http::field::connection, "close");
        resp.set("Mcp-Session-Id", session->session_id);
        resp.body() = sse_body;
        resp.prepare_payload();
        co_return resp;

    } else {
        // Session ID present: look up the session
        auto sid = std::string(session_id_it->value());
        auto session = get_session(sid);
        if (!session) {
            co_return error_response(
                http::status::not_found,
                "Session not found: " + sid);
        }

        if (session->closed) {
            co_return error_response(
                http::status::gone,
                "Session is closed");
        }

        // Push the client message to the session incoming queue
        session->push_incoming(body);

        spdlog::debug(
            "StreamableHttpServerTransport: POST received for session {}",
            sid);

        // For non-initialize requests, check if the Accept header prefers SSE
        // to stream back the response. Otherwise return 202 Accepted.
        bool wants_sse = (accept.find("text/event-stream") != std::string::npos);

        if (wants_sse) {
            // Wait for the service layer to produce a response
            json response_msg;
            bool got_response = false;

            auto deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (std::chrono::steady_clock::now() < deadline) {
                auto out = session->try_pop_outgoing();
                if (out.has_value()) {
                    response_msg = std::move(*out);
                    got_response = true;
                    break;
                }
                if (session->closed) break;

                boost::system::error_code ec;
                session->outgoing_signal->expires_after(
                    std::chrono::milliseconds(100));
                co_await session->outgoing_signal->async_wait(
                    asio::redirect_error(asio::use_awaitable, ec));
            }

            if (got_response) {
                auto event_id = std::to_string(session->next_event_id++);
                auto sse_body = format_sse_event(response_msg, event_id);

                http::response<http::string_body> resp{
                    http::status::ok, req.version()};
                resp.set(http::field::content_type, "text/event-stream");
                resp.set(http::field::cache_control, "no-cache");
                resp.set(http::field::connection, "close");
                resp.set("Mcp-Session-Id", session->session_id);
                resp.body() = sse_body;
                resp.prepare_payload();
                co_return resp;
            }
        }

        // Return 202 Accepted (response will come via SSE GET stream)
        http::response<http::string_body> resp{
            http::status::accepted, req.version()};
        resp.set(http::field::connection, "close");
        resp.set("Mcp-Session-Id", session->session_id);
        resp.prepare_payload();
        co_return resp;
    }
}

// =============================================================================
// GET handler (SSE stream)
// =============================================================================

asio::awaitable<void> StreamableHttpServerTransport::handle_get(
    tcp::socket& socket,
    http::request<http::string_body>& req) {
    // Validate Accept header includes text/event-stream
    auto accept = std::string(req[http::field::accept]);
    if (accept.find("text/event-stream") == std::string::npos
        && accept.find("*/*") == std::string::npos) {
        auto resp = error_response(
            http::status::not_acceptable,
            "Accept must include text/event-stream");
        resp.set(http::field::connection, "close");
        co_await http::async_write(
            socket, resp, asio::use_awaitable);
        co_return;
    }

    // Validate Mcp-Session-Id header
    auto session_id_it = req.find("Mcp-Session-Id");
    if (session_id_it == req.end()) {
        auto resp = error_response(
            http::status::bad_request,
            "Missing Mcp-Session-Id header");
        resp.set(http::field::connection, "close");
        co_await http::async_write(
            socket, resp, asio::use_awaitable);
        co_return;
    }

    auto sid = std::string(session_id_it->value());
    auto session = get_session(sid);
    if (!session) {
        auto resp = error_response(
            http::status::not_found,
            "Session not found: " + sid);
        resp.set(http::field::connection, "close");
        co_await http::async_write(
            socket, resp, asio::use_awaitable);
        co_return;
    }

    spdlog::info(
        "StreamableHttpServerTransport: SSE stream opened for session {}",
        sid);

    // Send the HTTP 200 response headers with SSE content type.
    // We use a chunked transfer encoding to stream data.
    http::response<http::empty_body> header{
        http::status::ok, req.version()};
    header.set(http::field::content_type, "text/event-stream");
    header.set(http::field::cache_control, "no-cache");
    header.set(http::field::connection, "keep-alive");
    header.set("Mcp-Session-Id", session->session_id);
    header.chunked(true);

    http::response_serializer<http::empty_body> header_sr{header};
    co_await http::async_write_header(
        socket, header_sr, asio::use_awaitable);

    // Send priming event if configured
    if (config_.sse_retry.has_value()) {
        auto event_id = session->next_event_id++;
        auto priming = format_sse_priming(
            event_id,
            static_cast<int>(config_.sse_retry->count()));
        // Write as a chunk
        auto chunk = beast::http::make_chunk(asio::buffer(priming));
        co_await asio::async_write(
            socket, chunk, asio::use_awaitable);
    }

    // SSE streaming loop
    auto keep_alive_interval = config_.sse_keep_alive;

    while (!session->closed && !closed_) {
        // Check for outgoing messages
        auto msg = session->try_pop_outgoing();
        if (msg.has_value()) {
            auto event_id = std::to_string(session->next_event_id++);
            auto event_str = format_sse_event(*msg, event_id);

            boost::system::error_code ec;
            auto chunk = beast::http::make_chunk(asio::buffer(event_str));
            co_await asio::async_write(
                socket, chunk,
                asio::redirect_error(asio::use_awaitable, ec));
            if (ec) {
                spdlog::debug(
                    "StreamableHttpServerTransport: SSE write error: {}",
                    ec.message());
                break;
            }
            continue;
        }

        // No messages available; wait for either a signal or keep-alive timeout
        boost::system::error_code ec;
        session->outgoing_signal->expires_after(keep_alive_interval);
        co_await session->outgoing_signal->async_wait(
            asio::redirect_error(asio::use_awaitable, ec));

        if (ec == asio::error::operation_aborted) {
            // Timer was cancelled, meaning a new message was pushed.
            // Loop back to check for messages.
            continue;
        }

        // Timer expired without cancellation: send a keep-alive comment
        auto keepalive = format_sse_keepalive();
        auto chunk = beast::http::make_chunk(asio::buffer(keepalive));
        co_await asio::async_write(
            socket, chunk,
            asio::redirect_error(asio::use_awaitable, ec));
        if (ec) {
            spdlog::debug(
                "StreamableHttpServerTransport: SSE keepalive write error: {}",
                ec.message());
            break;
        }
    }

    // Send the final chunk to close the chunked stream
    {
        boost::system::error_code ec;
        auto last = beast::http::make_chunk_last();
        co_await asio::async_write(
            socket, last,
            asio::redirect_error(asio::use_awaitable, ec));
    }

    spdlog::info(
        "StreamableHttpServerTransport: SSE stream closed for session {}",
        sid);
}

// =============================================================================
// DELETE handler
// =============================================================================

http::response<http::string_body> StreamableHttpServerTransport::handle_delete(
    http::request<http::string_body>& req) {
    auto session_id_it = req.find("Mcp-Session-Id");
    if (session_id_it == req.end()) {
        return error_response(
            http::status::bad_request,
            "Missing Mcp-Session-Id header");
    }

    auto sid = std::string(session_id_it->value());
    auto session = get_session(sid);
    if (!session) {
        return error_response(
            http::status::not_found,
            "Session not found: " + sid);
    }

    spdlog::info(
        "StreamableHttpServerTransport: session {} deleted by client",
        sid);

    // Mark the session as closed and signal all waiters
    session->closed = true;
    session->incoming_signal->cancel();
    session->outgoing_signal->cancel();

    http::response<http::string_body> resp{
        http::status::accepted, req.version()};
    resp.set(http::field::connection, "close");
    resp.prepare_payload();
    return resp;
}

// =============================================================================
// Error response helper
// =============================================================================

http::response<http::string_body> StreamableHttpServerTransport::error_response(
    http::status status, const std::string& message) {
    http::response<http::string_body> resp{status, 11};
    resp.set(http::field::content_type, "application/json");
    resp.set(http::field::connection, "close");
    json body = {{"error", message}};
    resp.body() = body.dump();
    resp.prepare_payload();
    return resp;
}

} // namespace mcp

#endif // MCP_HTTP_TRANSPORT
