#pragma once

#ifdef MCP_HTTP_TRANSPORT

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include <boost/asio.hpp>

#include "mcp/transport/session_id.hpp"
#include "mcp/transport/session_manager.hpp"
#include "mcp/transport/local_session.hpp"
#include "mcp/transport/worker_transport.hpp"

namespace mcp {

namespace asio = boost::asio;

// =============================================================================
// SessionConfig
// =============================================================================

struct SessionConfig {
    /// Maximum number of cached SSE events per stream (for resumption).
    size_t event_cache_size = 64;

    /// Optional retry hint sent in SSE priming events.
    std::optional<std::chrono::milliseconds> sse_retry_hint;
};

// =============================================================================
// LocalSessionManager
// =============================================================================

/// Internal handle for a live session.
struct LocalSessionHandle {
    SessionId id;

    /// The WorkerContext used to shuttle messages between the HTTP handler
    /// and the serve_server() loop.
    std::shared_ptr<WorkerContext<RoleServer>> worker_ctx;

    /// Per-HTTP-request SSE stream routing.
    /// Maps HttpRequestId -> the SSE stream sender for that request.
    std::unordered_map<HttpRequestId, HttpRequestWise> tx_router;

    /// Maps JSON-RPC request IDs and progress tokens to the HTTP request
    /// that should receive the response.
    std::unordered_map<ResourceKey, HttpRequestId> resource_router;

    /// Monotonically increasing event counter for this session.
    std::atomic<int64_t> next_event_index{1};

    /// The HttpRequestId of the standalone GET (common) stream, if any.
    std::optional<HttpRequestId> common_stream_id;

    /// Mutex for session-internal state (tx_router, resource_router, etc.)
    std::mutex mutex;

    bool closed = false;
};

/// Full stateful session manager with per-request message routing,
/// event caching, and SSE resume support.
///
/// Each session runs a WorkerTransport that bridges between the HTTP
/// handlers and serve_server(). Outgoing messages from the server handler
/// are routed to the correct HTTP request's SSE stream based on the
/// JSON-RPC request ID or progress token.
class LocalSessionManager : public SessionManager {
public:
    explicit LocalSessionManager(
        asio::any_io_executor executor,
        SessionConfig config = {});

    ~LocalSessionManager() override = default;

    // SessionManager interface
    asio::awaitable<SessionId> create_session() override;

    asio::awaitable<std::unique_ptr<Transport<RoleServer>>>
        initialize_session(const SessionId& id) override;

    bool has_session(const SessionId& id) const override;

    asio::awaitable<void> close_session(const SessionId& id) override;

    asio::awaitable<SseStream>
        create_stream(const SessionId& id, int64_t http_request_id) override;

    asio::awaitable<void> accept_message(
        const SessionId& id, int64_t http_request_id, json message) override;

    asio::awaitable<SseStream>
        create_standalone_stream(const SessionId& id) override;

    asio::awaitable<SseStream>
        resume(const SessionId& id, const EventId& last_event_id) override;

private:
    asio::any_io_executor executor_;
    SessionConfig config_;

    // Thread-safe session map
    mutable std::shared_mutex sessions_mutex_;
    std::unordered_map<std::string, std::shared_ptr<LocalSessionHandle>> sessions_;

    /// Get session handle by ID. Returns nullptr if not found.
    std::shared_ptr<LocalSessionHandle> get_handle(const SessionId& id) const;

    /// Route an outgoing message from the server handler to the correct
    /// SSE stream based on message type and routing tables.
    void route_outgoing(
        std::shared_ptr<LocalSessionHandle> handle,
        TxJsonRpcMessage<RoleServer> msg);
};

} // namespace mcp

#endif // MCP_HTTP_TRANSPORT
