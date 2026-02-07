#pragma once

#include <functional>
#include <memory>

#include <boost/asio.hpp>

#include "mcp/model/types.hpp"
#include "mcp/model/capabilities.hpp"
#include "mcp/model/completion.hpp"
#include "mcp/model/content.hpp"
#include "mcp/model/elicitation.hpp"
#include "mcp/model/init.hpp"
#include "mcp/model/logging.hpp"
#include "mcp/model/pagination.hpp"
#include "mcp/model/prompt.hpp"
#include "mcp/model/resource.hpp"
#include "mcp/model/roots.hpp"
#include "mcp/model/sampling.hpp"
#include "mcp/model/task.hpp"
#include "mcp/model/tool.hpp"
#include "mcp/model/error.hpp"
#include "mcp/model/unions.hpp"
#include "mcp/service/cancellation_token.hpp"
#include "mcp/service/peer.hpp"
#include "mcp/service/service_role.hpp"

namespace mcp {

namespace asio = boost::asio;

// Forward declaration
template <typename R>
class Peer;

/// Context provided to server request handlers.
struct ServerRequestContext {
    /// The peer (client) that sent the request
    std::shared_ptr<Peer<RoleServer>> peer;
    /// Request ID
    RequestId id;
    /// Cancellation token for this request
    CancellationToken cancellation;
    /// Extensions from the request
    Extensions extensions;
};

/// Server handler interface.
///
/// Override methods you want to handle; all default implementations
/// return METHOD_NOT_FOUND errors or empty results.
///
/// This mirrors the Rust ServerHandler trait with its ~25 virtual methods.
class ServerHandler {
public:
    virtual ~ServerHandler() = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    /// Called when the server is initialized. Override to return custom capabilities.
    virtual ServerCapabilities capabilities() {
        return ServerCapabilities{};
    }

    /// Called when initialization is complete (after InitializedNotification).
    virtual asio::awaitable<void> on_initialized(
        InitializeRequestParams client_info,
        ServerRequestContext ctx) {
        co_return;
    }

    // =========================================================================
    // Ping
    // =========================================================================

    virtual asio::awaitable<EmptyResult> on_ping(ServerRequestContext ctx) {
        co_return EmptyResult{};
    }

    // =========================================================================
    // Tools
    // =========================================================================

    virtual asio::awaitable<ListToolsResult> list_tools(
        std::optional<PaginatedRequestParams> params,
        ServerRequestContext ctx) {
        co_return ListToolsResult{};
    }

    virtual asio::awaitable<CallToolResult> call_tool(
        CallToolRequestParams params,
        ServerRequestContext ctx) {
        co_return CallToolResult::error({Content::text("Tool not found")});
    }

    // =========================================================================
    // Prompts
    // =========================================================================

    virtual asio::awaitable<ListPromptsResult> list_prompts(
        std::optional<PaginatedRequestParams> params,
        ServerRequestContext ctx) {
        co_return ListPromptsResult{};
    }

    virtual asio::awaitable<GetPromptResult> get_prompt(
        GetPromptRequestParams params,
        ServerRequestContext ctx) {
        co_return GetPromptResult{};
    }

    // =========================================================================
    // Resources
    // =========================================================================

    virtual asio::awaitable<ListResourcesResult> list_resources(
        std::optional<PaginatedRequestParams> params,
        ServerRequestContext ctx) {
        co_return ListResourcesResult{};
    }

    virtual asio::awaitable<ListResourceTemplatesResult> list_resource_templates(
        std::optional<PaginatedRequestParams> params,
        ServerRequestContext ctx) {
        co_return ListResourceTemplatesResult{};
    }

    virtual asio::awaitable<ReadResourceResult> read_resource(
        ReadResourceRequestParams params,
        ServerRequestContext ctx) {
        co_return ReadResourceResult{};
    }

    virtual asio::awaitable<EmptyResult> subscribe(
        SubscribeRequestParams params,
        ServerRequestContext ctx) {
        co_return EmptyResult{};
    }

    virtual asio::awaitable<EmptyResult> unsubscribe(
        UnsubscribeRequestParams params,
        ServerRequestContext ctx) {
        co_return EmptyResult{};
    }

    // =========================================================================
    // Logging
    // =========================================================================

    virtual asio::awaitable<EmptyResult> set_level(
        SetLevelRequestParams params,
        ServerRequestContext ctx) {
        co_return EmptyResult{};
    }

    // =========================================================================
    // Completion
    // =========================================================================

    virtual asio::awaitable<CompleteResult> complete(
        CompleteRequestParams params,
        ServerRequestContext ctx) {
        co_return CompleteResult{};
    }

    // =========================================================================
    // Tasks
    // =========================================================================

    virtual asio::awaitable<GetTaskInfoResult> get_task_info(
        GetTaskInfoParams params,
        ServerRequestContext ctx) {
        co_return GetTaskInfoResult{};
    }

    virtual asio::awaitable<ListTasksResult> list_tasks(
        std::optional<PaginatedRequestParams> params,
        ServerRequestContext ctx) {
        co_return ListTasksResult{};
    }

    virtual asio::awaitable<TaskResult> get_task_result(
        GetTaskResultParams params,
        ServerRequestContext ctx) {
        co_return TaskResult{};
    }

    virtual asio::awaitable<EmptyResult> cancel_task(
        CancelTaskParams params,
        ServerRequestContext ctx) {
        co_return EmptyResult{};
    }

    // =========================================================================
    // Custom
    // =========================================================================

    virtual asio::awaitable<json> on_custom_request(
        CustomRequest request,
        ServerRequestContext ctx) {
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

    virtual asio::awaitable<void> on_roots_list_changed() {
        co_return;
    }

    virtual asio::awaitable<void> on_custom_notification(
        CustomNotification notification) {
        co_return;
    }

    // =========================================================================
    // Dispatch (called by the service layer)
    // =========================================================================

    /// Dispatch a client request to the appropriate handler method.
    asio::awaitable<ServerResult> handle_request(
        ClientRequest request,
        ServerRequestContext ctx);

    /// Dispatch a client notification to the appropriate handler method.
    asio::awaitable<void> handle_notification(
        ClientNotification notification);
};

} // namespace mcp
