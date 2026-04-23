#ifdef MCP_HTTP_TRANSPORT

#include "mcp/transport/streamable_http_server.hpp"
#include "mcp/transport/oneshot_transport.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

namespace mcp {

namespace {

struct Authority {
    std::string host;  // lowercased, brackets stripped
    std::optional<uint16_t> port;
};

std::string lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Split "host" / "host:port" / "[v6]:port" / "[v6]" into host+port.
// Returns nullopt for malformed input.
std::optional<Authority> parse_authority(std::string_view s) {
    if (s.empty()) return std::nullopt;

    std::string host;
    std::optional<uint16_t> port;

    if (s.front() == '[') {
        auto close = s.find(']');
        if (close == std::string_view::npos) return std::nullopt;
        host = std::string(s.substr(1, close - 1));
        auto rest = s.substr(close + 1);
        if (!rest.empty()) {
            if (rest.front() != ':') return std::nullopt;
            rest.remove_prefix(1);
            try {
                port = static_cast<uint16_t>(std::stoi(std::string(rest)));
            } catch (...) { return std::nullopt; }
        }
    } else {
        auto colon = s.rfind(':');
        if (colon != std::string_view::npos
            && s.find(':') == colon) {
            // exactly one colon — treat as host:port
            host = std::string(s.substr(0, colon));
            try {
                port = static_cast<uint16_t>(std::stoi(std::string(s.substr(colon + 1))));
            } catch (...) { return std::nullopt; }
        } else if (s.find(':') == std::string_view::npos) {
            host = std::string(s);
        } else {
            // multiple colons without brackets — bare IPv6, no port
            host = std::string(s);
        }
    }

    if (host.empty()) return std::nullopt;
    return Authority{lowercase(std::move(host)), port};
}

bool host_is_allowed(
    const Authority& host,
    const std::vector<std::string>& allowed_hosts) {
    if (allowed_hosts.empty()) return true;  // opt-out
    for (const auto& entry : allowed_hosts) {
        auto allowed = parse_authority(entry);
        if (!allowed) continue;
        if (allowed->host != host.host) continue;
        if (allowed->port && allowed->port != host.port) continue;
        return true;
    }
    return false;
}

}  // namespace

// =============================================================================
// StreamableHttpServerTransport
// =============================================================================

StreamableHttpServerTransport::StreamableHttpServerTransport(
    asio::any_io_executor executor,
    StreamableHttpServerConfig config,
    std::shared_ptr<SessionManager> session_manager)
    : executor_(std::move(executor))
    , config_(std::move(config))
    , session_manager_(std::move(session_manager)) {}

StreamableHttpServerTransport::~StreamableHttpServerTransport() {
    // Best-effort cleanup; stop() should be called explicitly.
    if (acceptor_ && acceptor_->is_open()) {
        boost::system::error_code ec;
        acceptor_->close(ec);
    }
}

asio::awaitable<void> StreamableHttpServerTransport::start(
    std::shared_ptr<ServerHandler> handler,
    CancellationToken cancellation) {

    handler_ = std::move(handler);
    cancellation_ = std::move(cancellation);

    // Create session manager if not provided
    if (!session_manager_) {
        if (config_.stateful_mode) {
            SessionConfig sc;
            sc.event_cache_size = config_.event_cache_size;
            sc.sse_retry_hint = config_.sse_retry;
            session_manager_ = std::make_shared<LocalSessionManager>(executor_, sc);
        } else {
            session_manager_ = std::make_shared<NeverSessionManager>(executor_);
        }
    }

    // Bind and listen
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

    // Run the accept loop until cancellation
    co_await accept_loop();
}

void StreamableHttpServerTransport::stop() {
    cancellation_.cancel();

    if (acceptor_ && acceptor_->is_open()) {
        boost::system::error_code ec;
        acceptor_->close(ec);
        if (ec) {
            spdlog::warn(
                "StreamableHttpServerTransport::stop: error closing acceptor: {}",
                ec.message());
        }
    }

    // Close all running services
    {
        std::lock_guard<std::mutex> lock(services_mutex_);
        for (auto& [sid, service] : services_) {
            service.close();
        }
    }

    spdlog::info("StreamableHttpServerTransport: stopped");
}

uint16_t StreamableHttpServerTransport::port() const {
    return actual_port_;
}

// =============================================================================
// HTTP accept loop
// =============================================================================

asio::awaitable<void> StreamableHttpServerTransport::accept_loop() {
    while (!cancellation_.is_cancelled()) {
        boost::system::error_code ec;
        tcp::socket socket(executor_);
        co_await acceptor_->async_accept(
            socket, asio::redirect_error(asio::use_awaitable, ec));

        if (ec) {
            if (cancellation_.is_cancelled()) break;
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

        // Validate the Host header (DNS-rebinding protection).
        if (!config_.allowed_hosts.empty()) {
            auto host_hdr = req.find(http::field::host);
            if (host_hdr == req.end()) {
                co_await write_error(socket, http::status::bad_request,
                    "Bad Request: missing Host header", req.version());
                co_return;
            }
            auto host = parse_authority(std::string_view(host_hdr->value()));
            if (!host) {
                co_await write_error(socket, http::status::bad_request,
                    "Bad Request: invalid Host header", req.version());
                co_return;
            }
            if (!host_is_allowed(*host, config_.allowed_hosts)) {
                co_await write_error(socket, http::status::forbidden,
                    "Forbidden: Host header is not allowed", req.version());
                co_return;
            }
        }

        // Validate request target matches configured path
        auto target = std::string(req.target());
        auto query_pos = target.find('?');
        auto path = (query_pos != std::string::npos)
            ? target.substr(0, query_pos) : target;

        if (path != config_.path) {
            co_await write_error(socket, http::status::not_found,
                "Not found: " + path, req.version());
            co_return;
        }

        // Route based on HTTP method
        if (req.method() == http::verb::post) {
            co_await handle_post(socket, req);
        } else if (req.method() == http::verb::get) {
            co_await handle_get(socket, req);
        } else if (req.method() == http::verb::delete_) {
            co_await handle_delete(socket, req);
        } else {
            co_await write_error(socket, http::status::method_not_allowed,
                "Method not allowed", req.version());
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

asio::awaitable<void> StreamableHttpServerTransport::handle_post(
    tcp::socket& socket,
    http::request<http::string_body>& req) {

    // Validate Content-Type
    auto content_type = std::string(req[http::field::content_type]);
    if (content_type.find("application/json") == std::string::npos) {
        co_await write_error(socket, http::status::unsupported_media_type,
            "Content-Type must be application/json", req.version());
        co_return;
    }

    // Validate Accept header
    auto accept = std::string(req[http::field::accept]);
    if (accept.find("application/json") == std::string::npos
        && accept.find("text/event-stream") == std::string::npos
        && accept.find("*/*") == std::string::npos) {
        co_await write_error(socket, http::status::not_acceptable,
            "Accept must include application/json or text/event-stream",
            req.version());
        co_return;
    }

    // Parse the JSON body
    json body;
    std::string parse_error;
    try {
        body = json::parse(req.body());
    } catch (const std::exception& e) {
        parse_error = e.what();
    }
    if (!parse_error.empty()) {
        co_await write_error(socket, http::status::bad_request,
            "Invalid JSON: " + parse_error, req.version());
        co_return;
    }

    // Check for Mcp-Session-Id header
    auto session_id_it = req.find("Mcp-Session-Id");
    bool has_session_id = (session_id_it != req.end());

    if (!has_session_id) {
        // No session ID: this must be an initialize request (stateful mode)
        // or any request (stateless mode).

        bool is_initialize = false;
        if (body.is_object()
            && body.contains("method")
            && body["method"].is_string()) {
            is_initialize = (body["method"].get<std::string>() == "initialize");
        }

        if (config_.stateful_mode && !is_initialize) {
            co_await write_error(socket, http::status::bad_request,
                "First request must be an initialize request", req.version());
            co_return;
        }

        // Create a session (returns nullptr in stateless mode)
        auto sid = co_await session_manager_->create_session();

        if (sid) {
            // Stateful mode: create session, initialize transport, start service
            // IMPORTANT: The order here is critical to avoid deadlock:
            // 1. Initialize session (creates WorkerTransport)
            // 2. Create SSE stream (registers in tx_router so responses can be routed)
            // 3. Accept message (pushes to worker context)
            // 4. Start serve_server (which consumes from worker context)
            
            auto transport = co_await session_manager_->initialize_session(sid);
            auto http_request_id = next_http_request_id_.fetch_add(1);

            // Decide whether to stream SSE or return JSON
            bool wants_sse = (accept.find("text/event-stream") != std::string::npos
                              || accept.find("*/*") != std::string::npos)
                             && !config_.json_response_mode;

            // Create SSE stream BEFORE accepting message to ensure routing is ready
            auto sse_stream = co_await session_manager_->create_stream(
                sid, http_request_id);

            // Accept the message (pushes to worker context)
            co_await session_manager_->accept_message(sid, http_request_id, body);

            // Spawn serve_server in background - DO NOT co_await it!
            // serve_server() blocks waiting for the initialize handshake to complete,
            // but we need to start reading from the SSE stream immediately to send
            // the response back to the client. The client will then send the
            // initialized notification in a separate HTTP POST request.
            auto child_cancel = cancellation_.child();
            auto executor = co_await asio::this_coro::executor;
            auto services_mutex_ptr = &services_mutex_;
            auto services_ptr = &services_;
            auto sid_copy = sid;
            
            asio::co_spawn(executor,
                [transport = std::move(transport), handler = handler_,
                 child_cancel, services_mutex_ptr, services_ptr,
                 sid_copy]() mutable -> asio::awaitable<void> {
                    try {
                        auto running = co_await serve_server(
                            std::move(transport), handler, child_cancel);
                        
                        // Store the running service
                        {
                            std::lock_guard<std::mutex> lock(*services_mutex_ptr);
                            services_ptr->emplace(*sid_copy, std::move(running));
                        }
                    } catch (const std::exception& e) {
                        spdlog::error("serve_server failed: {}", e.what());
                    }
                },
                asio::detached);

            if (wants_sse) {
                // Send SSE response headers with session ID
                http::response<http::empty_body> header{
                    http::status::ok, req.version()};
                header.set(http::field::content_type, "text/event-stream");
                header.set(http::field::cache_control, "no-cache");
                header.set(http::field::connection, "keep-alive");
                header.set("Mcp-Session-Id", *sid);
                header.chunked(true);

                http::response_serializer<http::empty_body> header_sr{header};
                co_await http::async_write_header(
                    socket, header_sr, asio::use_awaitable);

                co_await stream_sse(socket, std::move(sse_stream));
            } else {
                // JSON response mode: stream was already created above

                // Read the first real message (skip priming)
                std::optional<json> response_json;
                while (true) {
                    auto msg_opt = co_await sse_stream.next();
                    if (!msg_opt) break;
                    if (msg_opt->message) {
                        response_json = std::move(msg_opt->message);
                        break;
                    }
                    // Skip priming events (no message payload)
                }

                if (response_json) {
                    http::response<http::string_body> resp{
                        http::status::ok, req.version()};
                    resp.set(http::field::content_type, "application/json");
                    resp.set(http::field::connection, "close");
                    resp.set("Mcp-Session-Id", *sid);
                    resp.body() = response_json->dump();
                    resp.prepare_payload();
                    co_await write_response(socket, std::move(resp));
                } else {
                    co_await write_error(socket, http::status::gateway_timeout,
                        "Timed out waiting for response", req.version());
                }
            }
        } else {
            // Stateless mode: create a OneshotTransport, serve, collect response
            RxJsonRpcMessage<RoleServer> rx_msg;
            std::string rx_parse_error;
            try {
                rx_msg = body.get<RxJsonRpcMessage<RoleServer>>();
            } catch (const std::exception& e) {
                rx_parse_error = e.what();
            }
            if (!rx_parse_error.empty()) {
                co_await write_error(socket, http::status::bad_request,
                    "Invalid JSON-RPC message: " + rx_parse_error,
                    req.version());
                co_return;
            }

            auto oneshot = std::make_unique<OneshotTransport>(
                executor_, std::move(rx_msg));
            auto oneshot_ptr = oneshot.get();

            auto child_cancel = cancellation_.child();
            auto running = co_await serve_server(
                std::move(oneshot), handler_, child_cancel);

            // Wait for the service to complete (oneshot transport auto-closes)
            co_await running.wait();

            // Collect responses
            auto responses = oneshot_ptr->take_responses();
            if (!responses.empty()) {
                json resp_json = responses.front();
                http::response<http::string_body> resp{
                    http::status::ok, req.version()};
                resp.set(http::field::content_type, "application/json");
                resp.set(http::field::connection, "close");
                resp.body() = resp_json.dump();
                resp.prepare_payload();
                co_await write_response(socket, std::move(resp));
            } else {
                http::response<http::string_body> resp{
                    http::status::accepted, req.version()};
                resp.set(http::field::connection, "close");
                resp.prepare_payload();
                co_await write_response(socket, std::move(resp));
            }
        }

    } else {
        // Session ID present: look up the session
        auto sid_str = std::string(session_id_it->value());
        auto sid = make_session_id(sid_str);

        if (!session_manager_->has_session(sid)) {
            co_await write_error(socket, http::status::not_found,
                "Session not found: " + sid_str, req.version());
            co_return;
        }

        auto http_request_id = next_http_request_id_.fetch_add(1);

        spdlog::debug(
            "StreamableHttpServerTransport: POST received for session {}",
            sid_str);

        // Check if client wants SSE streaming
        bool wants_sse = (accept.find("text/event-stream") != std::string::npos)
                         && !config_.json_response_mode;

        if (wants_sse) {
            // Create stream BEFORE accepting message to avoid race condition
            auto sse_stream = co_await session_manager_->create_stream(
                sid, http_request_id);

            // Now accept the message
            co_await session_manager_->accept_message(sid, http_request_id, body);

            // Send SSE response headers
            http::response<http::empty_body> header{
                http::status::ok, req.version()};
            header.set(http::field::content_type, "text/event-stream");
            header.set(http::field::cache_control, "no-cache");
            header.set(http::field::connection, "keep-alive");
            header.set("Mcp-Session-Id", sid_str);
            header.chunked(true);

            http::response_serializer<http::empty_body> header_sr{header};
            co_await http::async_write_header(
                socket, header_sr, asio::use_awaitable);

            co_await stream_sse(socket, std::move(sse_stream));
        } else {
            // JSON response mode: need to create stream and wait for response
            // Check if this is a notification (no id field) or a request
            bool is_notification = !body.contains("id");
            
            if (is_notification) {
                // Notifications don't expect a response
                co_await session_manager_->accept_message(sid, http_request_id, body);
                
                http::response<http::string_body> resp{
                    http::status::accepted, req.version()};
                resp.set(http::field::connection, "close");
                resp.set("Mcp-Session-Id", sid_str);
                resp.prepare_payload();
                co_await write_response(socket, std::move(resp));
            } else {
                // Request: create stream, accept message, wait for response
                auto sse_stream = co_await session_manager_->create_stream(
                    sid, http_request_id);
                
                co_await session_manager_->accept_message(sid, http_request_id, body);
                
                // Wait for response
                std::optional<json> response_json;
                while (true) {
                    auto msg_opt = co_await sse_stream.next();
                    if (!msg_opt) break;
                    if (msg_opt->message) {
                        response_json = std::move(msg_opt->message);
                        break;
                    }
                }
                
                if (response_json) {
                    http::response<http::string_body> resp{
                        http::status::ok, req.version()};
                    resp.set(http::field::content_type, "application/json");
                    resp.set(http::field::connection, "close");
                    resp.set("Mcp-Session-Id", sid_str);
                    resp.body() = response_json->dump();
                    resp.prepare_payload();
                    co_await write_response(socket, std::move(resp));
                } else {
                    co_await write_error(socket, http::status::gateway_timeout,
                        "Timed out waiting for response", req.version());
                }
            }
        }
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
        co_await write_error(socket, http::status::not_acceptable,
            "Accept must include text/event-stream", req.version());
        co_return;
    }

    // Validate Mcp-Session-Id header
    auto session_id_it = req.find("Mcp-Session-Id");
    if (session_id_it == req.end()) {
        co_await write_error(socket, http::status::bad_request,
            "Missing Mcp-Session-Id header", req.version());
        co_return;
    }

    auto sid_str = std::string(session_id_it->value());
    auto sid = make_session_id(sid_str);

    if (!session_manager_->has_session(sid)) {
        co_await write_error(socket, http::status::not_found,
            "Session not found: " + sid_str, req.version());
        co_return;
    }

    spdlog::info(
        "StreamableHttpServerTransport: SSE stream opened for session {}",
        sid_str);

    // Check for Last-Event-Id header (SSE reconnection)
    SseStream sse_stream{nullptr};
    auto last_event_id_it = req.find("Last-Event-Id");
    if (last_event_id_it != req.end()) {
        auto last_event_str = std::string(last_event_id_it->value());
        auto event_id = EventId::parse(last_event_str);
        if (event_id) {
            sse_stream = co_await session_manager_->resume(sid, *event_id);
        } else {
            spdlog::warn(
                "StreamableHttpServerTransport: invalid Last-Event-Id: {}",
                last_event_str);
            sse_stream = co_await session_manager_->create_standalone_stream(sid);
        }
    } else {
        sse_stream = co_await session_manager_->create_standalone_stream(sid);
    }

    // Send the HTTP 200 response headers with SSE content type
    http::response<http::empty_body> header{
        http::status::ok, req.version()};
    header.set(http::field::content_type, "text/event-stream");
    header.set(http::field::cache_control, "no-cache");
    header.set(http::field::connection, "keep-alive");
    header.set("Mcp-Session-Id", sid_str);
    header.chunked(true);

    http::response_serializer<http::empty_body> header_sr{header};
    co_await http::async_write_header(
        socket, header_sr, asio::use_awaitable);

    co_await stream_sse(socket, std::move(sse_stream));

    spdlog::info(
        "StreamableHttpServerTransport: SSE stream closed for session {}",
        sid_str);
}

// =============================================================================
// DELETE handler
// =============================================================================

asio::awaitable<void> StreamableHttpServerTransport::handle_delete(
    tcp::socket& socket,
    http::request<http::string_body>& req) {

    auto session_id_it = req.find("Mcp-Session-Id");
    if (session_id_it == req.end()) {
        co_await write_error(socket, http::status::bad_request,
            "Missing Mcp-Session-Id header", req.version());
        co_return;
    }

    auto sid_str = std::string(session_id_it->value());
    auto sid = make_session_id(sid_str);

    if (!session_manager_->has_session(sid)) {
        co_await write_error(socket, http::status::not_found,
            "Session not found: " + sid_str, req.version());
        co_return;
    }

    spdlog::info(
        "StreamableHttpServerTransport: session {} deleted by client",
        sid_str);

    // Close the session in the session manager
    co_await session_manager_->close_session(sid);

    // Close and remove the corresponding RunningService
    {
        std::lock_guard<std::mutex> lock(services_mutex_);
        auto it = services_.find(sid_str);
        if (it != services_.end()) {
            it->second.close();
            services_.erase(it);
        }
    }

    http::response<http::string_body> resp{
        http::status::ok, req.version()};
    resp.set(http::field::connection, "close");
    resp.prepare_payload();
    co_await write_response(socket, std::move(resp));
}

// =============================================================================
// SSE streaming helper
// =============================================================================

asio::awaitable<void> StreamableHttpServerTransport::stream_sse(
    tcp::socket& socket,
    SseStream stream) {

    auto keep_alive_interval = config_.sse_keep_alive;

    while (!cancellation_.is_cancelled()) {
        auto result = co_await stream.next_for(keep_alive_interval);

        if (result.closed()) {
            // Stream is closed — no more messages
            break;
        }

        std::string event_str;
        if (result.timed_out) {
            // Keepalive timeout — send comment
            event_str = ServerSseMessage::keepalive();
        } else {
            // Format and send the SSE event
            event_str = result.message->format();
        }

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
    }

    // Send the final chunk to close the chunked stream
    {
        boost::system::error_code ec;
        auto last = beast::http::make_chunk_last();
        co_await asio::async_write(
            socket, last,
            asio::redirect_error(asio::use_awaitable, ec));
    }
}

// =============================================================================
// HTTP response helpers
// =============================================================================

asio::awaitable<void> StreamableHttpServerTransport::write_error(
    tcp::socket& socket,
    http::status status,
    const std::string& message,
    unsigned version) {
    auto resp = error_response(status, message);
    resp.version(version);
    co_await http::async_write(socket, resp, asio::use_awaitable);
}

asio::awaitable<void> StreamableHttpServerTransport::write_response(
    tcp::socket& socket,
    http::response<http::string_body> resp) {
    co_await http::async_write(socket, resp, asio::use_awaitable);
}

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
