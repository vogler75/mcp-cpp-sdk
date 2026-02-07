#pragma once

#include "mcp/model/meta.hpp"
#include "mcp/service/cancellation_token.hpp"
#include "mcp/service/peer.hpp"
#include "mcp/service/service_role.hpp"

namespace mcp {

/// Request context provided to handlers for incoming requests.
///
/// Contains the peer, request metadata, cancellation token, and extensions.
template <typename R>
struct RequestContext {
    /// The peer that sent the request
    std::shared_ptr<Peer<R>> peer;
    /// The request ID
    RequestId id;
    /// Cancellation token for this request
    CancellationToken cancellation;
    /// Extensions (type-erased metadata)
    Extensions extensions;
    /// Meta from the request's _meta field
    Meta meta;
};

/// Notification context for incoming notifications.
template <typename R>
struct NotificationContext {
    /// The peer that sent the notification
    std::shared_ptr<Peer<R>> peer;
    /// Extensions
    Extensions extensions;
};

} // namespace mcp
