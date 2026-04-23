#include "mcp/handler/server_handler.hpp"

#include <spdlog/spdlog.h>

namespace mcp {

asio::awaitable<ServerResult> ServerHandler::handle_request(
    ClientRequest request,
    ServerRequestContext ctx) {
    if (request.is<PingRequest>()) {
        spdlog::trace("ServerHandler: dispatching PingRequest");
        auto result = co_await on_ping(std::move(ctx));
        co_return ServerResult(std::move(result));
    } else if (request.is<InitializeRequest>()) {
        // InitializeRequest is handled during the initialization handshake,
        // not dispatched through the handler.
        spdlog::trace("ServerHandler: ignoring InitializeRequest (handled during init)");
        throw McpError(
            ErrorData::method_not_found("initialize"));
    } else if (request.is<CompleteRequest>()) {
        spdlog::trace("ServerHandler: dispatching CompleteRequest");
        auto result = co_await complete(
            std::move(request.get<CompleteRequest>().params), std::move(ctx));
        co_return ServerResult(std::move(result));
    } else if (request.is<SetLevelRequest>()) {
        spdlog::trace("ServerHandler: dispatching SetLevelRequest");
        auto result = co_await set_level(
            std::move(request.get<SetLevelRequest>().params), std::move(ctx));
        co_return ServerResult(std::move(result));
    } else if (request.is<GetPromptRequest>()) {
        spdlog::trace("ServerHandler: dispatching GetPromptRequest");
        auto result = co_await get_prompt(
            std::move(request.get<GetPromptRequest>().params), std::move(ctx));
        co_return ServerResult(std::move(result));
    } else if (request.is<ListPromptsRequest>()) {
        spdlog::trace("ServerHandler: dispatching ListPromptsRequest");
        auto result = co_await list_prompts(
            request.get<ListPromptsRequest>().params, std::move(ctx));
        co_return ServerResult(std::move(result));
    } else if (request.is<ListResourcesRequest>()) {
        spdlog::trace("ServerHandler: dispatching ListResourcesRequest");
        auto result = co_await list_resources(
            request.get<ListResourcesRequest>().params, std::move(ctx));
        co_return ServerResult(std::move(result));
    } else if (request.is<ListResourceTemplatesRequest>()) {
        spdlog::trace("ServerHandler: dispatching ListResourceTemplatesRequest");
        auto result = co_await list_resource_templates(
            request.get<ListResourceTemplatesRequest>().params, std::move(ctx));
        co_return ServerResult(std::move(result));
    } else if (request.is<ReadResourceRequest>()) {
        spdlog::trace("ServerHandler: dispatching ReadResourceRequest");
        auto result = co_await read_resource(
            std::move(request.get<ReadResourceRequest>().params), std::move(ctx));
        co_return ServerResult(std::move(result));
    } else if (request.is<SubscribeRequest>()) {
        spdlog::trace("ServerHandler: dispatching SubscribeRequest");
        auto result = co_await subscribe(
            std::move(request.get<SubscribeRequest>().params), std::move(ctx));
        co_return ServerResult(std::move(result));
    } else if (request.is<UnsubscribeRequest>()) {
        spdlog::trace("ServerHandler: dispatching UnsubscribeRequest");
        auto result = co_await unsubscribe(
            std::move(request.get<UnsubscribeRequest>().params), std::move(ctx));
        co_return ServerResult(std::move(result));
    } else if (request.is<CallToolRequest>()) {
        spdlog::trace("ServerHandler: dispatching CallToolRequest");
        auto result = co_await call_tool(
            std::move(request.get<CallToolRequest>().params), std::move(ctx));
        co_return ServerResult(std::move(result));
    } else if (request.is<ListToolsRequest>()) {
        spdlog::trace("ServerHandler: dispatching ListToolsRequest");
        auto result = co_await list_tools(
            request.get<ListToolsRequest>().params, std::move(ctx));
        co_return ServerResult(std::move(result));
    } else if (request.is<GetTaskInfoRequest>()) {
        spdlog::trace("ServerHandler: dispatching GetTaskInfoRequest");
        auto result = co_await get_task_info(
            std::move(request.get<GetTaskInfoRequest>().params), std::move(ctx));
        co_return ServerResult(std::move(result));
    } else if (request.is<ListTasksRequest>()) {
        spdlog::trace("ServerHandler: dispatching ListTasksRequest");
        auto result = co_await list_tasks(
            request.get<ListTasksRequest>().params, std::move(ctx));
        co_return ServerResult(std::move(result));
    } else if (request.is<GetTaskResultRequest>()) {
        spdlog::trace("ServerHandler: dispatching GetTaskResultRequest");
        auto result = co_await get_task_result(
            std::move(request.get<GetTaskResultRequest>().params), std::move(ctx));
        co_return ServerResult(std::move(result));
    } else if (request.is<CancelTaskRequest>()) {
        spdlog::trace("ServerHandler: dispatching CancelTaskRequest");
        auto result = co_await cancel_task(
            std::move(request.get<CancelTaskRequest>().params), std::move(ctx));
        co_return ServerResult(std::move(result));
    } else if (request.is<CustomRequest>()) {
        spdlog::trace("ServerHandler: dispatching CustomRequest");
        auto result = co_await on_custom_request(
            request.get<CustomRequest>(), std::move(ctx));
        co_return ServerResult(CustomResult(std::move(result)));
    }

    throw McpError(ErrorData::method_not_found(request.method()));
}

asio::awaitable<void> ServerHandler::handle_notification(
    ClientNotification notification) {
    if (notification.is<CancelledNotification>()) {
        spdlog::trace("ServerHandler: dispatching CancelledNotification");
        co_await on_cancelled(notification.get<CancelledNotification>().params);
    } else if (notification.is<ProgressNotification>()) {
        spdlog::trace("ServerHandler: dispatching ProgressNotification");
        co_await on_progress(notification.get<ProgressNotification>().params);
    } else if (notification.is<InitializedNotification>()) {
        // on_initialized already fired after InitializeResult was sent; the
        // client's notifications/initialized is acknowledged here.
        spdlog::trace("ServerHandler: received InitializedNotification");
    } else if (notification.is<RootsListChangedNotification>()) {
        spdlog::trace("ServerHandler: dispatching RootsListChangedNotification");
        co_await on_roots_list_changed();
    } else if (notification.is<CustomNotification>()) {
        spdlog::trace("ServerHandler: dispatching CustomNotification");
        co_await on_custom_notification(notification.get<CustomNotification>());
    } else {
        spdlog::trace(
            "ServerHandler: ignoring unknown notification '{}'",
            notification.method());
    }
}

} // namespace mcp
