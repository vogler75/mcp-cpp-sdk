#include <iostream>
#include <boost/asio.hpp>
#include "mcp/mcp.hpp"

// A server handler using ToolRouter for calculator operations
class CalculatorHandler : public mcp::ToolRouterHandler {
public:
    CalculatorHandler() {
        // Register tools
        router_.add_tool(
            "add", "Add two numbers",
            mcp::json{{"type", "object"}, {"properties", {{"a", {{"type", "number"}}}, {"b", {{"type", "number"}}}}}, {"required", {"a", "b"}}},
            [](mcp::ToolCallContext ctx) -> boost::asio::awaitable<mcp::CallToolResult> {
                auto args = ctx.arguments.value_or(mcp::JsonObject{});
                double a = args.count("a") ? args.at("a").get<double>() : 0.0;
                double b = args.count("b") ? args.at("b").get<double>() : 0.0;
                co_return mcp::CallToolResult::success({mcp::Content::text(std::to_string(a + b))});
            });

        router_.add_tool(
            "subtract", "Subtract two numbers",
            mcp::json{{"type", "object"}, {"properties", {{"a", {{"type", "number"}}}, {"b", {{"type", "number"}}}}}, {"required", {"a", "b"}}},
            [](mcp::ToolCallContext ctx) -> boost::asio::awaitable<mcp::CallToolResult> {
                auto args = ctx.arguments.value_or(mcp::JsonObject{});
                double a = args.count("a") ? args.at("a").get<double>() : 0.0;
                double b = args.count("b") ? args.at("b").get<double>() : 0.0;
                co_return mcp::CallToolResult::success({mcp::Content::text(std::to_string(a - b))});
            });

        router_.add_tool(
            "multiply", "Multiply two numbers",
            mcp::json{{"type", "object"}, {"properties", {{"a", {{"type", "number"}}}, {"b", {{"type", "number"}}}}}, {"required", {"a", "b"}}},
            [](mcp::ToolCallContext ctx) -> boost::asio::awaitable<mcp::CallToolResult> {
                auto args = ctx.arguments.value_or(mcp::JsonObject{});
                double a = args.count("a") ? args.at("a").get<double>() : 0.0;
                double b = args.count("b") ? args.at("b").get<double>() : 0.0;
                co_return mcp::CallToolResult::success({mcp::Content::text(std::to_string(a * b))});
            });

        router_.add_tool(
            "divide", "Divide two numbers",
            mcp::json{{"type", "object"}, {"properties", {{"a", {{"type", "number"}}}, {"b", {{"type", "number"}}}}}, {"required", {"a", "b"}}},
            [](mcp::ToolCallContext ctx) -> boost::asio::awaitable<mcp::CallToolResult> {
                auto args = ctx.arguments.value_or(mcp::JsonObject{});
                double a = args.count("a") ? args.at("a").get<double>() : 0.0;
                double b = args.count("b") ? args.at("b").get<double>() : 0.0;
                if (b == 0.0) {
                    co_return mcp::CallToolResult::error({mcp::Content::text("Division by zero")});
                }
                co_return mcp::CallToolResult::success({mcp::Content::text(std::to_string(a / b))});
            });
    }
};

int main() {
    boost::asio::io_context io;
    auto handler = std::make_shared<CalculatorHandler>();

    // Create stdio transport and serve
    auto transport = std::make_unique<mcp::StdioTransport<mcp::RoleServer>>(io.get_executor());

    boost::asio::co_spawn(io, [&]() -> boost::asio::awaitable<void> {
        auto service = co_await mcp::serve_server(std::move(transport), handler);
        co_await service.wait();
    }, boost::asio::detached);

    io.run();
    return 0;
}
