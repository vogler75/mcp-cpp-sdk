#pragma once

#ifdef MCP_HTTP_TRANSPORT

#include <memory>

#include <boost/asio.hpp>

#include "mcp/transport/session_id.hpp"
#include "mcp/transport/sse_message.hpp"
#include "mcp/transport/sse_stream.hpp"
#include "mcp/transport/transport.hpp"
#include "mcp/service/service_role.hpp"

namespace mcp {

namespace asio = boost::asio;

// =============================================================================
// SessionManager — abstract interface for managing HTTP sessions
// =============================================================================

/// Abstract session manager interface.
///
/// Implementations manage the lifecycle of MCP sessions within the HTTP
/// server transport. The server transport delegates all session-related
/// operations to this interface.
///
/// Two built-in implementations are provided:
/// - LocalSessionManager: Full stateful session management with per-request
///   message routing, event caching, and SSE resume support.
/// - NeverSessionManager: Stateless mode — each POST gets an OneshotTransport.
class SessionManager {
public:
    virtual ~SessionManager() = default;

    /// Create a new session, returning its ID.
    virtual asio::awaitable<SessionId> create_session() = 0;

    /// Called when the session receives an initialize request.
    /// Returns a Transport to wire into serve_server().
    virtual asio::awaitable<std::unique_ptr<Transport<RoleServer>>>
        initialize_session(const SessionId& id) = 0;

    /// Check whether a session exists.
    virtual bool has_session(const SessionId& id) const = 0;

    /// Close and remove a session.
    virtual asio::awaitable<void> close_session(const SessionId& id) = 0;

    /// Create an SSE stream for a specific HTTP request within a session.
    /// Returns an SseStream that yields ServerSseMessages for this request.
    virtual asio::awaitable<SseStream>
        create_stream(const SessionId& id, int64_t http_request_id) = 0;

    /// Accept a JSON-RPC message into a session, associated with a specific
    /// HTTP request ID for response routing.
    virtual asio::awaitable<void> accept_message(
        const SessionId& id, int64_t http_request_id, json message) = 0;

    /// Create a standalone SSE stream (GET endpoint) for server-initiated
    /// messages (notifications, pings, etc.).
    virtual asio::awaitable<SseStream>
        create_standalone_stream(const SessionId& id) = 0;

    /// Resume a stream from a given event ID (for Last-Event-Id reconnection).
    /// Replays cached events starting from the given ID, then continues live.
    virtual asio::awaitable<SseStream>
        resume(const SessionId& id, const EventId& last_event_id) = 0;
};

} // namespace mcp

#endif // MCP_HTTP_TRANSPORT
