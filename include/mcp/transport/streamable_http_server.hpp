#pragma once

#ifdef MCP_HTTP_TRANSPORT

#include <string>
#include <optional>
#include <memory>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>

#include "mcp/transport/session_id.hpp"
#include "mcp/transport/session_manager.hpp"
#include "mcp/transport/local_session_manager.hpp"
#include "mcp/transport/never_session_manager.hpp"
#include "mcp/handler/server_handler.hpp"
#include "mcp/service/service.hpp"
#include "mcp/service/cancellation_token.hpp"

namespace mcp {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

/// Configuration for the streamable HTTP server transport.
struct StreamableHttpServerConfig {
    /// Host to bind to (default: "0.0.0.0")
    std::string host = "0.0.0.0";

    /// Port to listen on
    uint16_t port = 0;  // 0 = auto-assign

    /// Path prefix for MCP endpoints (default: "/mcp")
    std::string path = "/mcp";

    /// SSE keep-alive interval (default: 15 seconds)
    std::chrono::seconds sse_keep_alive{15};

    /// SSE retry hint sent to clients in priming event (default: 3 seconds)
    std::optional<std::chrono::milliseconds> sse_retry{std::chrono::milliseconds{3000}};

    /// Whether to require stateful sessions (default: true)
    bool stateful_mode = true;

    /// Maximum number of cached SSE events per stream (for resumption).
    size_t event_cache_size = 64;

    /// If true, POST responses use application/json instead of SSE.
    /// The server still supports GET for SSE streaming.
    bool json_response_mode = false;

    /// Allowed hostnames or `host:port` authorities for inbound `Host`
    /// header validation. Defaults to loopback-only to block DNS-rebinding
    /// attacks against locally running servers. Public deployments should
    /// override this with their own hostnames.
    ///     allowed_hosts = {"example.com", "example.com:8080"}
    /// An empty vector disables the check and accepts any Host header
    /// (NOT recommended for public deployments).
    std::vector<std::string> allowed_hosts{"localhost", "127.0.0.1", "::1"};
};

/// Streamable HTTP server for MCP.
///
/// This is NOT a Transport — it is a service host that manages sessions
/// and routes HTTP requests to the appropriate handlers. Each session
/// gets its own Transport internally (via the SessionManager).
///
/// Usage:
/// ```cpp
/// StreamableHttpServerTransport server(executor, config);
/// co_await server.start(handler, cancellation);
/// ```
///
/// The server handles:
/// - POST /mcp : Client sends JSON-RPC messages
/// - GET /mcp  : Client opens SSE stream for server messages
/// - DELETE /mcp : Client closes session
class StreamableHttpServerTransport {
public:
    explicit StreamableHttpServerTransport(
        asio::any_io_executor executor,
        StreamableHttpServerConfig config = {},
        std::shared_ptr<SessionManager> session_manager = nullptr);

    ~StreamableHttpServerTransport();

    /// Start the HTTP server and block until cancellation or error.
    /// This binds, listens, and runs the accept loop.
    asio::awaitable<void> start(
        std::shared_ptr<ServerHandler> handler,
        CancellationToken cancellation = {});

    /// Stop the server. Safe to call from any thread.
    void stop();

    /// Get the actual port the server is listening on.
    /// Only valid after start() has been called.
    uint16_t port() const;

private:
    asio::any_io_executor executor_;
    StreamableHttpServerConfig config_;

    // TCP acceptor
    std::shared_ptr<tcp::acceptor> acceptor_;
    uint16_t actual_port_ = 0;

    // Session manager (created on start if not provided)
    std::shared_ptr<SessionManager> session_manager_;

    // Handler and cancellation (set on start)
    std::shared_ptr<ServerHandler> handler_;
    CancellationToken cancellation_;

    // HTTP request ID counter
    std::atomic<int64_t> next_http_request_id_{1};

    // Running services per session (for stateful mode)
    std::mutex services_mutex_;
    std::unordered_map<std::string, RunningService<RoleServer>> services_;

    /// Accept loop - runs until cancellation.
    asio::awaitable<void> accept_loop();

    /// Handle a single HTTP connection.
    asio::awaitable<void> handle_connection(tcp::socket socket);

    /// Handle POST request — may stream SSE or return JSON.
    asio::awaitable<void> handle_post(
        tcp::socket& socket,
        http::request<http::string_body>& req);

    /// Handle GET request (SSE stream).
    asio::awaitable<void> handle_get(
        tcp::socket& socket,
        http::request<http::string_body>& req);

    /// Handle DELETE request.
    asio::awaitable<void> handle_delete(
        tcp::socket& socket,
        http::request<http::string_body>& req);

    /// Stream SSE events from an SseStream to a socket.
    asio::awaitable<void> stream_sse(
        tcp::socket& socket,
        SseStream stream);

    /// Write an HTTP error response.
    static asio::awaitable<void> write_error(
        tcp::socket& socket,
        http::status status,
        const std::string& message,
        unsigned version = 11);

    /// Write an HTTP response with a string body.
    static asio::awaitable<void> write_response(
        tcp::socket& socket,
        http::response<http::string_body> resp);

    /// Build error response.
    static http::response<http::string_body> error_response(
        http::status status, const std::string& message);
};

} // namespace mcp

#endif // MCP_HTTP_TRANSPORT
