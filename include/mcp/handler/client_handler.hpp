#pragma once

#include <memory>

#include <boost/asio.hpp>

#include "mcp/model/types.hpp"
#include "mcp/model/error.hpp"
#include "mcp/model/elicitation.hpp"
#include "mcp/model/init.hpp"
#include "mcp/model/notifications.hpp"
#include "mcp/model/roots.hpp"
#include "mcp/model/sampling.hpp"
#include "mcp/model/unions.hpp"
#include "mcp/service/cancellation_token.hpp"
#include "mcp/service/peer.hpp"
#include "mcp/service/service_role.hpp"

namespace mcp {

namespace asio = boost::asio;

// Forward declaration
template <typename R>
class Peer;

/// Context provided to client request handlers.
struct ClientRequestContext {
    /// The peer (server) that sent the request
    std::shared_ptr<Peer<RoleClient>> peer;
    /// Request ID
    RequestId id;
    /// Cancellation token for this request
    CancellationToken cancellation;
    /// Extensions from the request
    Extensions extensions;
};

/// Client handler interface.
///
/// Override methods you want to handle; all default implementations
/// return METHOD_NOT_FOUND errors or empty results.
///
/// This mirrors the Rust ClientHandler trait with its ~14 virtual methods.
class ClientHandler {
public:
    virtual ~ClientHandler() = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    /// Called when the client is initialized. Override to return custom capabilities.
    virtual ClientCapabilities capabilities() {
        return ClientCapabilities{};
    }

    /// Called after initialization is complete.
    virtual asio::awaitable<void> on_initialized(
        InitializeResult server_info,
        ClientRequestContext ctx) {
        co_return;
    }

    // =========================================================================
    // Ping
    // =========================================================================

    virtual asio::awaitable<EmptyResult> on_ping(ClientRequestContext ctx) {
        co_return EmptyResult{};
    }

    // =========================================================================
    // Sampling
    // =========================================================================

    /// Handle a createMessage request from the server.
    virtual asio::awaitable<CreateMessageResult> create_message(
        CreateMessageRequestParams params,
        ClientRequestContext ctx) {
        throw McpError(ErrorData::method_not_found("sampling/createMessage"));
    }

    // =========================================================================
    // Roots
    // =========================================================================

    /// Handle a listRoots request from the server.
    virtual asio::awaitable<ListRootsResult> list_roots(
        ClientRequestContext ctx) {
        co_return ListRootsResult{};
    }

    // =========================================================================
    // Elicitation
    // =========================================================================

    /// Handle an elicitation/create request from the server.
    virtual asio::awaitable<CreateElicitationResult> create_elicitation(
        CreateElicitationRequestParams params,
        ClientRequestContext ctx) {
        throw McpError(ErrorData::method_not_found("elicitation/create"));
    }

    // =========================================================================
    // Custom
    // =========================================================================

    virtual asio::awaitable<json> on_custom_request(
        CustomRequest request,
        ClientRequestContext ctx) {
        throw McpError(ErrorData::method_not_found(request.method));
    }

    // =========================================================================
    // Notifications
    // =========================================================================

    virtual asio::awaitable<void> on_cancelled(
        CancelledNotificationParam params) {
        co_return;
    }

    virtual asio::awaitable<void> on_progress(
        ProgressNotificationParam params) {
        co_return;
    }

    virtual asio::awaitable<void> on_logging_message(
        LoggingMessageNotificationParam params) {
        co_return;
    }

    virtual asio::awaitable<void> on_resource_updated(
        ResourceUpdatedNotificationParam params) {
        co_return;
    }

    virtual asio::awaitable<void> on_resource_list_changed() {
        co_return;
    }

    virtual asio::awaitable<void> on_tool_list_changed() {
        co_return;
    }

    virtual asio::awaitable<void> on_prompt_list_changed() {
        co_return;
    }

    virtual asio::awaitable<void> on_custom_notification(
        CustomNotification notification) {
        co_return;
    }

    // =========================================================================
    // Dispatch (called by the service layer)
    // =========================================================================

    /// Dispatch a server request to the appropriate handler method.
    asio::awaitable<ClientResult> handle_request(
        ServerRequest request,
        ClientRequestContext ctx);

    /// Dispatch a server notification to the appropriate handler method.
    asio::awaitable<void> handle_notification(
        ServerNotification notification);
};

} // namespace mcp
