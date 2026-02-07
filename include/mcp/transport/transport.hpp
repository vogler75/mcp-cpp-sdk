#pragma once

#include <memory>
#include <optional>

#include <boost/asio.hpp>

#include "mcp/model/jsonrpc.hpp"
#include "mcp/model/unions.hpp"
#include "mcp/service/service_role.hpp"

namespace mcp {

namespace asio = boost::asio;

// =============================================================================
// Transport message type aliases
// =============================================================================

/// The JSON-RPC message type transmitted by a role R
template <typename R>
using TxJsonRpcMessage = JsonRpcMessage<
    typename R::Req,
    typename R::Resp,
    typename R::Not>;

/// The JSON-RPC message type received by a role R
template <typename R>
using RxJsonRpcMessage = JsonRpcMessage<
    typename R::PeerReq,
    typename R::PeerResp,
    typename R::PeerNot>;

// =============================================================================
// Transport abstract base class
// =============================================================================

/// Abstract transport interface for sending and receiving JSON-RPC messages.
///
/// Implementations handle the details of message framing and delivery
/// over different mediums (stdio, HTTP, etc.).
///
/// Template parameter R is the service role (RoleClient or RoleServer).
template <typename R>
class Transport {
    static_assert(is_service_role_v<R>, "R must be RoleClient or RoleServer");

public:
    virtual ~Transport() = default;

    /// Send a message to the peer.
    virtual asio::awaitable<void> send(TxJsonRpcMessage<R> msg) = 0;

    /// Receive a message from the peer.
    /// Returns std::nullopt when the transport is closed/EOF.
    virtual asio::awaitable<std::optional<RxJsonRpcMessage<R>>> receive() = 0;

    /// Close the transport. After this, send/receive should not be called.
    virtual asio::awaitable<void> close() = 0;
};

/// Convenience alias for a unique_ptr to a transport
template <typename R>
using TransportPtr = std::unique_ptr<Transport<R>>;

} // namespace mcp
