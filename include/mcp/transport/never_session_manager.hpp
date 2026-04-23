#pragma once

#ifdef MCP_HTTP_TRANSPORT

#include <memory>

#include <boost/asio.hpp>

#include "mcp/transport/session_manager.hpp"
#include "mcp/transport/oneshot_transport.hpp"

namespace mcp {

namespace asio = boost::asio;

// =============================================================================
// NeverSessionManager — stateless mode (no sessions, no SSE)
// =============================================================================

/// Stateless session manager where each POST gets a fresh OneshotTransport.
/// No session IDs are tracked. GET and DELETE endpoints are not supported.
/// SSE streams and resume are not supported.
///
/// This is used when the server operates in "stateless" mode: every POST
/// creates an independent request-response cycle with no persistent state.
class NeverSessionManager : public SessionManager {
public:
    explicit NeverSessionManager(asio::any_io_executor executor);

    ~NeverSessionManager() override = default;

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
};

} // namespace mcp

#endif // MCP_HTTP_TRANSPORT
