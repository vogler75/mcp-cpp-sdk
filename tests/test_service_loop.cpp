#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>

#include <boost/asio.hpp>

#include "mcp/handler/tool_router.hpp"
#include "mcp/service/service.hpp"
#include "mcp/transport/transport.hpp"

using namespace mcp;
namespace asio = boost::asio;
using json = nlohmann::json;

// =============================================================================
// In-process channel transport
//
// Two TransportChannel objects form a bidirectional pipe.  Each channel is a
// thread-safe queue of JSON messages signalled via a steady_timer.
// =============================================================================

struct TransportChannel {
    std::queue<json> messages;
    std::mutex mutex;
    std::shared_ptr<asio::steady_timer> signal;
    bool closed = false;

    explicit TransportChannel(asio::any_io_executor exec)
        : signal(std::make_shared<asio::steady_timer>(exec)) {
        signal->expires_at(asio::steady_timer::time_point::max());
    }

    void push(json msg) {
        std::lock_guard lock(mutex);
        messages.push(std::move(msg));
        signal->cancel();
    }

    void close_channel() {
        std::lock_guard lock(mutex);
        closed = true;
        signal->cancel();
    }
};

/// A Transport implementation backed by two TransportChannels.
/// Messages sent go into send_ch_; messages received come from recv_ch_.
template <typename R>
class ChannelTransport : public Transport<R> {
public:
    ChannelTransport(
        std::shared_ptr<TransportChannel> send_ch,
        std::shared_ptr<TransportChannel> recv_ch)
        : send_ch_(std::move(send_ch)), recv_ch_(std::move(recv_ch)) {}

    asio::awaitable<void> send(TxJsonRpcMessage<R> msg) override {
        json j = msg;
        send_ch_->push(std::move(j));
        co_return;
    }

    asio::awaitable<std::optional<RxJsonRpcMessage<R>>> receive() override {
        while (true) {
            {
                std::lock_guard lock(recv_ch_->mutex);
                if (!recv_ch_->messages.empty()) {
                    auto j = std::move(recv_ch_->messages.front());
                    recv_ch_->messages.pop();
                    co_return j.template get<RxJsonRpcMessage<R>>();
                }
                if (recv_ch_->closed) {
                    co_return std::nullopt;
                }
            }
            // Wait for the signal timer to be cancelled (= new message or close).
            boost::system::error_code ec;
            co_await recv_ch_->signal->async_wait(
                asio::redirect_error(asio::use_awaitable, ec));
            // Reset the timer so the next iteration can wait again.
            recv_ch_->signal->expires_at(asio::steady_timer::time_point::max());
        }
    }

    asio::awaitable<void> close() override {
        send_ch_->close_channel();
        co_return;
    }

private:
    std::shared_ptr<TransportChannel> send_ch_;
    std::shared_ptr<TransportChannel> recv_ch_;
};

// =============================================================================
// Test server handler with a couple of tools
// =============================================================================

class TestServerHandler : public ToolRouterHandler {
public:
    TestServerHandler() {
        router_.add_tool(
            "add",
            "Add two numbers",
            json{{"type", "object"},
                 {"properties",
                  {{"a", {{"type", "number"}}}, {"b", {{"type", "number"}}}}}},
            [](ToolCallContext ctx) -> asio::awaitable<CallToolResult> {
                auto args = ctx.arguments.value_or(JsonObject{});
                int a = args.count("a") ? args.at("a").get<int>() : 0;
                int b = args.count("b") ? args.at("b").get<int>() : 0;
                co_return CallToolResult::success(
                    {Content::text(std::to_string(a + b))});
            });

        router_.add_tool(
            "echo",
            "Echo a message",
            json{{"type", "object"},
                 {"properties", {{"message", {{"type", "string"}}}}}},
            [](ToolCallContext ctx) -> asio::awaitable<CallToolResult> {
                auto args = ctx.arguments.value_or(JsonObject{});
                std::string msg =
                    args.count("message") ? args.at("message").get<std::string>() : "";
                co_return CallToolResult::success({Content::text(msg)});
            });
    }
};

// =============================================================================
// Helpers
// =============================================================================

/// Holds the result of a test for later assertion.
/// GTest macros cannot be used inside coroutines (they use `return;`
/// which is incompatible with co_return), so we collect data in the
/// coroutine and assert outside.
struct TestResult {
    bool completed = false;
    std::string error;
    json data;
};

/// Wait for a JSON message on a channel.
static asio::awaitable<json> wait_for_message(
    std::shared_ptr<TransportChannel> ch) {
    while (true) {
        {
            std::lock_guard lock(ch->mutex);
            if (!ch->messages.empty()) {
                auto j = std::move(ch->messages.front());
                ch->messages.pop();
                co_return j;
            }
        }
        boost::system::error_code ec;
        co_await ch->signal->async_wait(
            asio::redirect_error(asio::use_awaitable, ec));
        ch->signal->expires_at(asio::steady_timer::time_point::max());
    }
}

/// Test driver: set up a server, perform the MCP init handshake manually
/// as the "client", then run arbitrary requests against the server.
///
/// - to_server: channel to push messages that the server will receive
/// - from_server: channel to read messages the server sends
///
/// The test body sends raw JSON-RPC messages and reads raw JSON responses.
///
/// This avoids using serve_client entirely, which means we don't have
/// Peer::send_request's mutex issue, and we don't have the client receive
/// loop competing for messages.
using ServerTestBody = std::function<asio::awaitable<TestResult>(
    std::shared_ptr<TransportChannel> to_server,
    std::shared_ptr<TransportChannel> from_server)>;

static TestResult run_server_test(ServerTestBody body) {
    asio::io_context io;
    auto exec = io.get_executor();

    auto to_server = std::make_shared<TransportChannel>(exec);
    auto from_server = std::make_shared<TransportChannel>(exec);

    auto server_transport = std::make_unique<ChannelTransport<RoleServer>>(
        from_server, to_server);

    auto server_handler = std::make_shared<TestServerHandler>();

    TestResult result;

    asio::co_spawn(
        io,
        [&]() -> asio::awaitable<void> {
            CancellationToken cancel;

            // Spawn the server in the background.
            std::optional<RunningService<RoleServer>> server_svc;

            asio::co_spawn(
                co_await asio::this_coro::executor,
                [&]() -> asio::awaitable<void> {
                    server_svc = co_await serve_server(
                        std::move(server_transport), server_handler, cancel);
                },
                asio::detached);

            // --- Perform MCP init handshake manually as the "client" ---

            // 1) Send InitializeRequest
            json init_req = {
                {"jsonrpc", "2.0"},
                {"id", 0},
                {"method", "initialize"},
                {"params", {
                    {"protocolVersion", ProtocolVersion::LATEST.value()},
                    {"capabilities", json::object()},
                    {"clientInfo", {{"name", "test-client"}, {"version", "0.1.0"}}}
                }}
            };
            to_server->push(init_req);

            // 2) Wait for InitializeResult response
            auto init_resp = co_await wait_for_message(from_server);

            // 3) Send InitializedNotification
            json initialized_notif = {
                {"jsonrpc", "2.0"},
                {"method", "notifications/initialized"}
            };
            to_server->push(initialized_notif);

            // Small yield to let the server process the notification.
            asio::steady_timer t(co_await asio::this_coro::executor);
            t.expires_after(std::chrono::milliseconds(50));
            co_await t.async_wait(asio::use_awaitable);

            // --- Init done. Run the test body. ---
            try {
                result = co_await body(to_server, from_server);
                // Store the init response for the InitHandshake test.
                if (!result.data.contains("init_response")) {
                    result.data["init_response"] = init_resp;
                }
            } catch (const std::exception& e) {
                result.error = std::string("Test body threw: ") + e.what();
            }

            // Shut down.
            if (server_svc) {
                server_svc->cancel();
            }
            cancel.cancel();
            to_server->close_channel();
        },
        asio::detached);

    io.run_for(std::chrono::seconds(10));
    return result;
}

// =============================================================================
// Tests
// =============================================================================

TEST(ServiceLoop, InitHandshake) {
    auto result = run_server_test(
        [](std::shared_ptr<TransportChannel>,
           std::shared_ptr<TransportChannel>)
           -> asio::awaitable<TestResult> {
            // If we got here, the handshake succeeded (done in run_server_test).
            TestResult r;
            r.data = json::object();
            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;

    // Verify the init response.
    auto& init_resp = result.data["init_response"];
    ASSERT_TRUE(init_resp.contains("result")) << init_resp.dump();
    EXPECT_EQ(init_resp["result"]["protocolVersion"],
              ProtocolVersion::LATEST.value());
    EXPECT_TRUE(init_resp["result"].contains("capabilities"));
    EXPECT_TRUE(init_resp["result"]["capabilities"].contains("tools"));
}

TEST(ServiceLoop, ListTools) {
    auto result = run_server_test(
        [](std::shared_ptr<TransportChannel> to_server,
           std::shared_ptr<TransportChannel> from_server)
           -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            json req = {
                {"jsonrpc", "2.0"},
                {"id", 100},
                {"method", "tools/list"}
            };
            to_server->push(req);

            auto resp = co_await wait_for_message(from_server);
            r.data["response"] = resp;
            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;

    auto& resp = result.data["response"];
    EXPECT_EQ(resp["jsonrpc"], "2.0");
    EXPECT_EQ(resp["id"], 100);
    ASSERT_TRUE(resp.contains("result")) << resp.dump();

    auto list_result = resp["result"].get<ListToolsResult>();
    ASSERT_EQ(list_result.tools.size(), 2);

    bool found_add = false;
    bool found_echo = false;
    for (const auto& tool : list_result.tools) {
        if (tool.name == "add") found_add = true;
        if (tool.name == "echo") found_echo = true;
    }
    EXPECT_TRUE(found_add) << "Expected 'add' tool in listing";
    EXPECT_TRUE(found_echo) << "Expected 'echo' tool in listing";
}

TEST(ServiceLoop, CallTool) {
    auto result = run_server_test(
        [](std::shared_ptr<TransportChannel> to_server,
           std::shared_ptr<TransportChannel> from_server)
           -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            json req = {
                {"jsonrpc", "2.0"},
                {"id", 101},
                {"method", "tools/call"},
                {"params", {{"name", "add"}, {"arguments", {{"a", 10}, {"b", 32}}}}}
            };
            to_server->push(req);

            auto resp = co_await wait_for_message(from_server);
            r.data["response"] = resp;
            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;

    auto& resp = result.data["response"];
    EXPECT_EQ(resp["jsonrpc"], "2.0");
    EXPECT_EQ(resp["id"], 101);
    ASSERT_TRUE(resp.contains("result")) << resp.dump();

    auto call_result = resp["result"].get<CallToolResult>();
    EXPECT_FALSE(call_result.is_error.value_or(false));
    ASSERT_EQ(call_result.content.size(), 1);
    EXPECT_TRUE(call_result.content[0].is_text());
    EXPECT_EQ(call_result.content[0].as_text()->text, "42");
}

TEST(ServiceLoop, CallToolEcho) {
    auto result = run_server_test(
        [](std::shared_ptr<TransportChannel> to_server,
           std::shared_ptr<TransportChannel> from_server)
           -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            json req = {
                {"jsonrpc", "2.0"},
                {"id", 102},
                {"method", "tools/call"},
                {"params", {
                    {"name", "echo"},
                    {"arguments", {{"message", "hello world"}}}
                }}
            };
            to_server->push(req);

            auto resp = co_await wait_for_message(from_server);
            r.data["response"] = resp;
            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;

    auto& resp = result.data["response"];
    EXPECT_EQ(resp["jsonrpc"], "2.0");
    EXPECT_EQ(resp["id"], 102);
    ASSERT_TRUE(resp.contains("result")) << resp.dump();

    auto call_result = resp["result"].get<CallToolResult>();
    EXPECT_FALSE(call_result.is_error.value_or(false));
    ASSERT_EQ(call_result.content.size(), 1);
    EXPECT_TRUE(call_result.content[0].is_text());
    EXPECT_EQ(call_result.content[0].as_text()->text, "hello world");
}

TEST(ServiceLoop, CallToolNotFound) {
    auto result = run_server_test(
        [](std::shared_ptr<TransportChannel> to_server,
           std::shared_ptr<TransportChannel> from_server)
           -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            json req = {
                {"jsonrpc", "2.0"},
                {"id", 103},
                {"method", "tools/call"},
                {"params", {{"name", "nonexistent_tool"}}}
            };
            to_server->push(req);

            auto resp = co_await wait_for_message(from_server);
            r.data["response"] = resp;
            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;

    auto& resp = result.data["response"];
    EXPECT_EQ(resp["jsonrpc"], "2.0");
    EXPECT_EQ(resp["id"], 103);
    ASSERT_TRUE(resp.contains("result")) << resp.dump();

    auto call_result = resp["result"].get<CallToolResult>();
    EXPECT_TRUE(call_result.is_error.value_or(false));
}

TEST(ServiceLoop, Ping) {
    auto result = run_server_test(
        [](std::shared_ptr<TransportChannel> to_server,
           std::shared_ptr<TransportChannel> from_server)
           -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            json req = {
                {"jsonrpc", "2.0"},
                {"id", 104},
                {"method", "ping"}
            };
            to_server->push(req);

            auto resp = co_await wait_for_message(from_server);
            r.data["response"] = resp;
            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;

    auto& resp = result.data["response"];
    EXPECT_EQ(resp["jsonrpc"], "2.0");
    EXPECT_EQ(resp["id"], 104);
    ASSERT_TRUE(resp.contains("result")) << resp.dump();
    EXPECT_TRUE(resp["result"].is_object());
}

TEST(ServiceLoop, MultipleRequestsSequential) {
    auto result = run_server_test(
        [](std::shared_ptr<TransportChannel> to_server,
           std::shared_ptr<TransportChannel> from_server)
           -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();
            r.data["responses"] = json::array();

            for (int i = 0; i < 5; ++i) {
                json req = {
                    {"jsonrpc", "2.0"},
                    {"id", 200 + i},
                    {"method", "tools/call"},
                    {"params", {
                        {"name", "add"},
                        {"arguments", {{"a", i}, {"b", i}}}
                    }}
                };
                to_server->push(req);

                auto resp = co_await wait_for_message(from_server);
                r.data["responses"].push_back(resp);
            }

            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;

    auto& responses = result.data["responses"];
    ASSERT_EQ(responses.size(), 5);

    for (int i = 0; i < 5; ++i) {
        auto& resp = responses[i];
        EXPECT_EQ(resp["jsonrpc"], "2.0") << "iteration " << i;
        EXPECT_EQ(resp["id"], 200 + i) << "iteration " << i;
        ASSERT_TRUE(resp.contains("result")) << "iteration " << i
            << ": " << resp.dump();

        auto call_result = resp["result"].get<CallToolResult>();
        EXPECT_FALSE(call_result.is_error.value_or(false)) << "iteration " << i;
        ASSERT_EQ(call_result.content.size(), 1) << "iteration " << i;
        EXPECT_TRUE(call_result.content[0].is_text()) << "iteration " << i;
        EXPECT_EQ(call_result.content[0].as_text()->text, std::to_string(i + i))
            << "iteration " << i;
    }
}
