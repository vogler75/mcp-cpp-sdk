#include "mcp/handler/client_handler.hpp"

#include <spdlog/spdlog.h>

namespace mcp {

asio::awaitable<ClientResult> ClientHandler::handle_request(
    ServerRequest request,
    ClientRequestContext ctx) {
    if (request.is<PingRequest>()) {
        spdlog::trace("ClientHandler: dispatching PingRequest");
        auto result = co_await on_ping(std::move(ctx));
        co_return ClientResult(std::move(result));
    } else if (request.is<CreateMessageRequest>()) {
        spdlog::trace("ClientHandler: dispatching CreateMessageRequest");
        auto result = co_await create_message(
            request.get<CreateMessageRequest>().params, std::move(ctx));
        co_return ClientResult(std::move(result));
    } else if (request.is<ListRootsRequest>()) {
        spdlog::trace("ClientHandler: dispatching ListRootsRequest");
        auto result = co_await list_roots(std::move(ctx));
        co_return ClientResult(std::move(result));
    } else if (request.is<CreateElicitationRequest>()) {
        spdlog::trace("ClientHandler: dispatching CreateElicitationRequest");
        auto result = co_await create_elicitation(
            request.get<CreateElicitationRequest>().params, std::move(ctx));
        co_return ClientResult(std::move(result));
    } else if (request.is<CustomRequest>()) {
        spdlog::trace("ClientHandler: dispatching CustomRequest");
        auto result = co_await on_custom_request(
            request.get<CustomRequest>(), std::move(ctx));
        co_return ClientResult(CustomResult(std::move(result)));
    }

    throw McpError(ErrorData::method_not_found(request.method()));
}

asio::awaitable<void> ClientHandler::handle_notification(
    ServerNotification notification) {
    if (notification.is<CancelledNotification>()) {
        spdlog::trace("ClientHandler: dispatching CancelledNotification");
        co_await on_cancelled(notification.get<CancelledNotification>().params);
    } else if (notification.is<ProgressNotification>()) {
        spdlog::trace("ClientHandler: dispatching ProgressNotification");
        co_await on_progress(notification.get<ProgressNotification>().params);
    } else if (notification.is<LoggingMessageNotification>()) {
        spdlog::trace("ClientHandler: dispatching LoggingMessageNotification");
        co_await on_logging_message(
            notification.get<LoggingMessageNotification>().params);
    } else if (notification.is<ResourceUpdatedNotification>()) {
        spdlog::trace("ClientHandler: dispatching ResourceUpdatedNotification");
        co_await on_resource_updated(
            notification.get<ResourceUpdatedNotification>().params);
    } else if (notification.is<ResourceListChangedNotification>()) {
        spdlog::trace("ClientHandler: dispatching ResourceListChangedNotification");
        co_await on_resource_list_changed();
    } else if (notification.is<ToolListChangedNotification>()) {
        spdlog::trace("ClientHandler: dispatching ToolListChangedNotification");
        co_await on_tool_list_changed();
    } else if (notification.is<PromptListChangedNotification>()) {
        spdlog::trace("ClientHandler: dispatching PromptListChangedNotification");
        co_await on_prompt_list_changed();
    } else if (notification.is<CustomNotification>()) {
        spdlog::trace("ClientHandler: dispatching CustomNotification");
        co_await on_custom_notification(notification.get<CustomNotification>());
    } else {
        spdlog::trace(
            "ClientHandler: ignoring unknown notification '{}'",
            notification.method());
    }
}

} // namespace mcp
