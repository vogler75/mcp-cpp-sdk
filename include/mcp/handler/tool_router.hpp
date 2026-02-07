#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>

#include "mcp/handler/server_handler.hpp"
#include "mcp/model/tool.hpp"
#include "mcp/model/unions.hpp"

namespace mcp {

namespace asio = boost::asio;

/// Context passed to tool call handler functions.
struct ToolCallContext {
    /// The tool name being called
    std::string name;
    /// The arguments passed to the tool
    std::optional<JsonObject> arguments;
    /// The server request context
    ServerRequestContext server_ctx;
};

/// A single tool route: metadata + handler function.
struct ToolRoute {
    /// The tool metadata (name, description, schema)
    Tool tool;

    /// The async handler function
    using Handler = std::function<asio::awaitable<CallToolResult>(ToolCallContext)>;
    Handler handler;
};

/// Routes tool calls by name to registered handler functions.
///
/// This is the C++ equivalent of Rust's ToolRouter. Register tools
/// with their handlers, then use as_handler() to get call_tool/list_tools
/// implementations.
class ToolRouter {
public:
    ToolRouter() = default;

    /// Add a tool route.
    void add_route(Tool tool, ToolRoute::Handler handler) {
        std::string name(tool.name);
        routes_.emplace(name, ToolRoute{std::move(tool), std::move(handler)});
    }

    /// Add a tool route with a simpler signature.
    template <typename F>
    void add_tool(
        const std::string& name,
        const std::string& description,
        json input_schema,
        F&& handler) {
        auto schema_ptr = std::make_shared<JsonObject>(
            input_schema.is_object()
                ? input_schema.get<JsonObject>()
                : JsonObject{});

        Tool tool;
        tool.name = name;
        tool.description = description;
        tool.input_schema = schema_ptr;

        add_route(std::move(tool), std::forward<F>(handler));
    }

    /// List all registered tools.
    std::vector<Tool> list_tools() const {
        std::vector<Tool> tools;
        tools.reserve(routes_.size());
        for (const auto& [name, route] : routes_) {
            tools.push_back(route.tool);
        }
        return tools;
    }

    /// Get a tool by name.
    const Tool* get_tool(const std::string& name) const {
        auto it = routes_.find(name);
        if (it == routes_.end()) return nullptr;
        return &it->second.tool;
    }

    /// Call a tool by name. Returns error if tool not found.
    asio::awaitable<CallToolResult> call_tool(ToolCallContext ctx) {
        auto it = routes_.find(ctx.name);
        if (it == routes_.end()) {
            co_return CallToolResult::error(
                {Content::text("Tool not found: " + ctx.name)});
        }
        co_return co_await it->second.handler(std::move(ctx));
    }

    /// Check if a tool is registered.
    bool has_tool(const std::string& name) const {
        return routes_.count(name) > 0;
    }

    /// Get the number of registered tools.
    size_t size() const { return routes_.size(); }

    /// Check if empty.
    bool empty() const { return routes_.empty(); }

private:
    std::unordered_map<std::string, ToolRoute> routes_;
};

/// A ServerHandler that uses a ToolRouter for tool dispatch.
///
/// Subclass this and set up the router in your constructor, or compose
/// it with other handler functionality.
class ToolRouterHandler : public ServerHandler {
public:
    ServerCapabilities capabilities() override {
        auto caps = ServerCapabilities{};
        if (!router_.empty()) {
            caps.tools = ToolsCapability{};
        }
        return caps;
    }

    asio::awaitable<ListToolsResult> list_tools(
        std::optional<PaginatedRequestParams>,
        ServerRequestContext) override {
        ListToolsResult result;
        result.tools = router_.list_tools();
        co_return result;
    }

    asio::awaitable<CallToolResult> call_tool(
        CallToolRequestParams params,
        ServerRequestContext ctx) override {
        ToolCallContext tool_ctx{
            std::string(params.name),
            std::move(params.arguments),
            std::move(ctx)
        };
        co_return co_await router_.call_tool(std::move(tool_ctx));
    }

protected:
    ToolRouter router_;
};

} // namespace mcp
