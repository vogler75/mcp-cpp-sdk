#pragma once

#ifdef MCP_HTTP_TRANSPORT

#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <boost/asio.hpp>

#include "mcp/transport/transport.hpp"
#include "mcp/service/service_role.hpp"

namespace mcp {

namespace asio = boost::asio;

// =============================================================================
// OneshotTransport — stateless single request-response transport
// =============================================================================

/// A transport that delivers exactly one received message and collects
/// the response(s). Used for stateless HTTP mode where each POST creates
/// a fresh transport, processes one request-response cycle, and closes.
///
/// Usage:
/// 1. Construct with the incoming JSON-RPC message
/// 2. Pass to serve_server() which will call receive() then send()
/// 3. After the service completes, call take_responses() to get results
class OneshotTransport : public Transport<RoleServer> {
public:
    /// Create a OneshotTransport with the initial message to deliver.
    explicit OneshotTransport(
        asio::any_io_executor executor,
        RxJsonRpcMessage<RoleServer> initial_message);

    ~OneshotTransport() override = default;

    /// Send a message (response) — stores it for later retrieval.
    asio::awaitable<void> send(TxJsonRpcMessage<RoleServer> msg) override;

    /// Receive a message — returns the initial message on first call,
    /// then nullopt (transport is done).
    asio::awaitable<std::optional<RxJsonRpcMessage<RoleServer>>> receive() override;

    /// Close the transport.
    asio::awaitable<void> close() override;

    /// Get the collected response(s) after the transport is done.
    std::vector<TxJsonRpcMessage<RoleServer>> take_responses();

    /// Check if we have any responses collected.
    bool has_responses() const;

private:
    asio::any_io_executor executor_;
    std::optional<RxJsonRpcMessage<RoleServer>> initial_;
    bool delivered_ = false;
    bool closed_ = false;

    std::vector<TxJsonRpcMessage<RoleServer>> responses_;
    mutable std::mutex mutex_;

    /// Signal for waiting on responses
    std::shared_ptr<asio::steady_timer> response_signal_;
};

} // namespace mcp

#endif // MCP_HTTP_TRANSPORT
