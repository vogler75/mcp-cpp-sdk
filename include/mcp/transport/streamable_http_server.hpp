#pragma once

#ifdef MCP_HTTP_TRANSPORT

#include <string>
#include <optional>
#include <memory>
#include <functional>
#include <queue>
#include <mutex>
#include <atomic>
#include <unordered_map>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>

#include "mcp/transport/transport.hpp"
#include "mcp/service/service_role.hpp"

namespace mcp {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;
using json = nlohmann::json;

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
};

/// Represents a single client session on the server side.
/// Each session has its own message queues for bidirectional communication.
struct ServerSession {
    std::string session_id;

    // Messages from client to server (received via POST)
    std::queue<json> incoming_messages;
    std::mutex incoming_mutex;
    std::shared_ptr<asio::steady_timer> incoming_signal;

    // Messages from server to client (sent via SSE)
    std::queue<json> outgoing_messages;
    std::mutex outgoing_mutex;
    std::shared_ptr<asio::steady_timer> outgoing_signal;

    // SSE event tracking
    int64_t next_event_id = 1;

    bool closed = false;

    explicit ServerSession(asio::any_io_executor executor, std::string id);

    void push_incoming(json msg);
    std::optional<json> try_pop_incoming();

    void push_outgoing(json msg);
    std::optional<json> try_pop_outgoing();
};

/// Format a JSON-RPC message as an SSE event string.
std::string format_sse_event(
    const json& message,
    const std::optional<std::string>& event_id = std::nullopt,
    const std::optional<int>& retry_ms = std::nullopt);

/// Format an SSE keep-alive comment.
std::string format_sse_keepalive();

/// Format an SSE priming event (empty data with retry hint).
std::string format_sse_priming(int64_t event_id, int retry_ms);

/// Generate a UUID v4 session ID.
std::string generate_session_id();

/// Streamable HTTP server transport.
///
/// Implements Transport<RoleServer> by running an HTTP server that
/// accepts MCP client connections. The server handles:
///
/// - POST /mcp : Client sends JSON-RPC messages
/// - GET /mcp  : Client opens SSE stream for server messages
/// - DELETE /mcp : Client closes session
///
/// The transport manages a single active session. When a client connects
/// and sends an initialize request, a new session is created.
class StreamableHttpServerTransport : public Transport<RoleServer> {
public:
    explicit StreamableHttpServerTransport(
        asio::any_io_executor executor,
        StreamableHttpServerConfig config = {});

    ~StreamableHttpServerTransport() override;

    /// Start the HTTP server (must be called before using as transport).
    asio::awaitable<void> start();

    /// Get the actual port the server is listening on.
    uint16_t port() const;

    // Transport interface
    asio::awaitable<void> send(TxJsonRpcMessage<RoleServer> msg) override;
    asio::awaitable<std::optional<RxJsonRpcMessage<RoleServer>>> receive() override;
    asio::awaitable<void> close() override;

private:
    asio::any_io_executor executor_;
    StreamableHttpServerConfig config_;

    // TCP acceptor
    std::shared_ptr<tcp::acceptor> acceptor_;
    uint16_t actual_port_ = 0;

    // Session management
    std::shared_ptr<ServerSession> active_session_;
    std::mutex session_mutex_;

    bool closed_ = false;

    /// Accept loop - runs in background.
    asio::awaitable<void> accept_loop();

    /// Handle a single HTTP connection.
    asio::awaitable<void> handle_connection(tcp::socket socket);

    /// Handle POST request.
    asio::awaitable<http::response<http::string_body>> handle_post(
        http::request<http::string_body>& req);

    /// Handle GET request (SSE stream).
    asio::awaitable<void> handle_get(
        tcp::socket& socket,
        http::request<http::string_body>& req);

    /// Handle DELETE request.
    http::response<http::string_body> handle_delete(
        http::request<http::string_body>& req);

    /// Get or create the active session.
    std::shared_ptr<ServerSession> get_session(const std::string& session_id);

    /// Create a new session.
    std::shared_ptr<ServerSession> create_session();

    /// Build error response.
    static http::response<http::string_body> error_response(
        http::status status, const std::string& message);
};

} // namespace mcp

#endif // MCP_HTTP_TRANSPORT
