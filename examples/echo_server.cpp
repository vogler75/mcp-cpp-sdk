#include <iostream>
#include <boost/asio.hpp>
#include "mcp/mcp.hpp"

class EchoHandler : public mcp::ToolRouterHandler {
public:
    EchoHandler() {
        router_.add_tool(
            "echo", "Echo back the input message",
            mcp::json{{"type", "object"}, {"properties", {{"message", {{"type", "string"}}}}}, {"required", {"message"}}},
            [](mcp::ToolCallContext ctx) -> boost::asio::awaitable<mcp::CallToolResult> {
                auto args = ctx.arguments.value_or(mcp::JsonObject{});
                std::string message = args.count("message") ? args.at("message").get<std::string>() : "";
                co_return mcp::CallToolResult::success({mcp::Content::text("Echo: " + message)});
            });
    }
};

int main() {
    boost::asio::io_context io;
    auto handler = std::make_shared<EchoHandler>();
    auto transport = std::make_unique<mcp::StdioTransport<mcp::RoleServer>>(io.get_executor());

    boost::asio::co_spawn(io, [&]() -> boost::asio::awaitable<void> {
        auto service = co_await mcp::serve_server(std::move(transport), handler);
        co_await service.wait();
    }, boost::asio::detached);

    io.run();
    return 0;
}
