#pragma once

#include <type_traits>

#include "mcp/model/unions.hpp"

namespace mcp {

// =============================================================================
// Service Role Traits (replaces Rust's ServiceRole trait with associated types)
// =============================================================================

/// RoleClient: The client side of an MCP connection.
/// - Sends ClientRequests/ClientNotifications
/// - Receives ServerRequests/ServerNotifications
struct RoleClient {
    using Req = ClientRequest;
    using Resp = ClientResult;
    using Not = ClientNotification;
    using PeerReq = ServerRequest;
    using PeerResp = ServerResult;
    using PeerNot = ServerNotification;
    using Info = InitializeRequestParams;
    using PeerInfo = InitializeResult;
    static constexpr bool is_client = true;
    static constexpr bool is_server = false;
};

/// RoleServer: The server side of an MCP connection.
/// - Sends ServerRequests/ServerNotifications
/// - Receives ClientRequests/ClientNotifications
struct RoleServer {
    using Req = ServerRequest;
    using Resp = ServerResult;
    using Not = ServerNotification;
    using PeerReq = ClientRequest;
    using PeerResp = ClientResult;
    using PeerNot = ClientNotification;
    using Info = InitializeResult;
    using PeerInfo = InitializeRequestParams;
    static constexpr bool is_client = false;
    static constexpr bool is_server = true;
};

/// Type trait to check if R is a valid service role
template <typename R>
struct is_service_role : std::false_type {};

template <>
struct is_service_role<RoleClient> : std::true_type {};

template <>
struct is_service_role<RoleServer> : std::true_type {};

template <typename R>
inline constexpr bool is_service_role_v = is_service_role<R>::value;

/// The message types that a role transmits (outgoing)
template <typename R>
using TxRequest = typename R::Req;

template <typename R>
using TxResult = typename R::Resp;

template <typename R>
using TxNotification = typename R::Not;

/// The message types that a role receives (incoming)
template <typename R>
using RxRequest = typename R::PeerReq;

template <typename R>
using RxResult = typename R::PeerResp;

template <typename R>
using RxNotification = typename R::PeerNot;

} // namespace mcp
