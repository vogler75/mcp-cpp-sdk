#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include "mcp/handler/tool_router.hpp"

using namespace mcp;
namespace asio = boost::asio;

TEST(ToolRouter, AddAndListTools) {
    ToolRouter router;
    router.add_tool("greet", "Greet someone", json{{"type", "object"}},
        [](ToolCallContext) -> asio::awaitable<CallToolResult> {
            co_return CallToolResult::success({Content::text("Hello!")});
        });
    router.add_tool("farewell", "Say goodbye", json{{"type", "object"}},
        [](ToolCallContext) -> asio::awaitable<CallToolResult> {
            co_return CallToolResult::success({Content::text("Bye!")});
        });

    EXPECT_EQ(router.size(), 2);
    EXPECT_TRUE(router.has_tool("greet"));
    EXPECT_TRUE(router.has_tool("farewell"));
    EXPECT_FALSE(router.has_tool("nonexistent"));

    auto tools = router.list_tools();
    EXPECT_EQ(tools.size(), 2);
}

TEST(ToolRouter, GetTool) {
    ToolRouter router;
    router.add_tool("test", "Test tool", json{{"type", "object"}},
        [](ToolCallContext) -> asio::awaitable<CallToolResult> {
            co_return CallToolResult::success({Content::text("ok")});
        });

    auto* tool = router.get_tool("test");
    ASSERT_NE(tool, nullptr);
    EXPECT_EQ(tool->name, "test");
    EXPECT_EQ(*tool->description, "Test tool");

    EXPECT_EQ(router.get_tool("missing"), nullptr);
}

TEST(ToolRouter, CallToolDispatch) {
    asio::io_context io;
    ToolRouter router;
    router.add_tool("add", "Add numbers", json{{"type", "object"}},
        [](ToolCallContext ctx) -> asio::awaitable<CallToolResult> {
            auto args = ctx.arguments.value_or(JsonObject{});
            int a = args.count("a") ? args.at("a").get<int>() : 0;
            int b = args.count("b") ? args.at("b").get<int>() : 0;
            co_return CallToolResult::success({Content::text(std::to_string(a + b))});
        });

    bool done = false;
    asio::co_spawn(io, [&]() -> asio::awaitable<void> {
        ToolCallContext ctx;
        ctx.name = "add";
        ctx.arguments = JsonObject{{"a", 3}, {"b", 4}};
        auto result = co_await router.call_tool(std::move(ctx));
        EXPECT_FALSE(result.is_error.value_or(false));
        EXPECT_EQ(result.content.size(), 1);
        // Content inherits from RawContent, so we can call is_text/as_text directly
        EXPECT_TRUE(result.content[0].is_text());
        EXPECT_EQ(result.content[0].as_text()->text, "7");
        done = true;
    }, asio::detached);

    io.run();
    EXPECT_TRUE(done);
}

TEST(ToolRouter, CallToolNotFound) {
    asio::io_context io;
    ToolRouter router;

    bool done = false;
    asio::co_spawn(io, [&]() -> asio::awaitable<void> {
        ToolCallContext ctx;
        ctx.name = "nonexistent";
        auto result = co_await router.call_tool(std::move(ctx));
        // Should return error result
        EXPECT_TRUE(result.is_error.value_or(false));
        done = true;
    }, asio::detached);

    io.run();
    EXPECT_TRUE(done);
}

TEST(ToolRouter, ToolRouterHandlerCapabilities) {
    class TestHandler : public ToolRouterHandler {
    public:
        TestHandler() {
            router_.add_tool("test", "Test", json{{"type", "object"}},
                [](ToolCallContext) -> asio::awaitable<CallToolResult> {
                    co_return CallToolResult::success({Content::text("ok")});
                });
        }
    };

    TestHandler handler;
    auto caps = handler.capabilities();
    EXPECT_TRUE(caps.tools.has_value());
}
