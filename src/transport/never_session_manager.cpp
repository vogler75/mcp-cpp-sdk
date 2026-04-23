#ifdef MCP_HTTP_TRANSPORT

#include "mcp/transport/never_session_manager.hpp"

#include <stdexcept>

#include <spdlog/spdlog.h>

namespace mcp {

NeverSessionManager::NeverSessionManager(asio::any_io_executor executor)
    : executor_(std::move(executor)) {}

asio::awaitable<SessionId> NeverSessionManager::create_session() {
    // Stateless mode: no sessions are created.
    // Return a nil SessionId to signal stateless operation.
    co_return nullptr;
}

asio::awaitable<std::unique_ptr<Transport<RoleServer>>>
NeverSessionManager::initialize_session(const SessionId& /*id*/) {
    // Stateless mode: should not be called. The server transport handles
    // stateless POSTs by creating OneshotTransport directly.
    throw std::logic_error(
        "NeverSessionManager: initialize_session should not be called in stateless mode");
}

bool NeverSessionManager::has_session(const SessionId& /*id*/) const {
    // No sessions exist in stateless mode.
    return false;
}

asio::awaitable<void> NeverSessionManager::close_session(const SessionId& /*id*/) {
    // Nothing to close in stateless mode.
    co_return;
}

asio::awaitable<SseStream>
NeverSessionManager::create_stream(const SessionId& /*id*/, int64_t /*http_request_id*/) {
    throw std::logic_error(
        "NeverSessionManager: SSE streams are not supported in stateless mode");
}

asio::awaitable<void> NeverSessionManager::accept_message(
    const SessionId& /*id*/, int64_t /*http_request_id*/, json /*message*/) {
    throw std::logic_error(
        "NeverSessionManager: accept_message should not be called in stateless mode");
}

asio::awaitable<SseStream>
NeverSessionManager::create_standalone_stream(const SessionId& /*id*/) {
    throw std::logic_error(
        "NeverSessionManager: standalone SSE streams are not supported in stateless mode");
}

asio::awaitable<SseStream>
NeverSessionManager::resume(const SessionId& /*id*/, const EventId& /*last_event_id*/) {
    throw std::logic_error(
        "NeverSessionManager: SSE resume is not supported in stateless mode");
}

} // namespace mcp

#endif // MCP_HTTP_TRANSPORT
