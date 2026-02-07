/// simple_client.cpp -- MCP C++ SDK client example
///
/// Demonstrates how to use the client-side of the MCP SDK to connect to a
/// server, complete the initialization handshake, list available tools, and
/// call a tool -- all within one process.
///
/// Uses in-memory channel transports (backed by queues and timer signalling)
/// to connect the client and server sides without any real I/O. This is the
/// same pattern used by the SDK's own integration tests.

#include <iostream>
#include <mutex>
#include <queue>

#include <boost/asio.hpp>

#include "mcp/mcp.hpp"

namespace asio = boost::asio;
using namespace mcp;

// =============================================================================
// In-memory channel transport
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

/// Create a connected pair of transports for client and server.
auto make_transport_pair(asio::any_io_executor exec) {
    auto ch1 = std::make_shared<TransportChannel>(exec); // client -> server
    auto ch2 = std::make_shared<TransportChannel>(exec); // server -> client
    auto client_transport = std::make_unique<ChannelTransport<RoleClient>>(ch1, ch2);
    auto server_transport = std::make_unique<ChannelTransport<RoleServer>>(ch2, ch1);
    return std::make_pair(std::move(client_transport), std::move(server_transport));
}

// =============================================================================
// Server handler -- a simple tool server with two tools
// =============================================================================

/// A server handler that provides "greet" and "add" tools.
class SimpleServerHandler : public ToolRouterHandler {
public:
    SimpleServerHandler() {
        // Tool 1: greet -- greets someone by name
        router_.add_tool(
            "greet", "Greet someone by name",
            json{
                {"type", "object"},
                {"properties",
                 {{"name", {{"type", "string"}, {"description", "Name to greet"}}}}},
                {"required", {"name"}}},
            [](ToolCallContext ctx) -> asio::awaitable<CallToolResult> {
                auto args = ctx.arguments.value_or(JsonObject{});
                std::string name =
                    args.count("name") ? args.at("name").get<std::string>() : "World";
                co_return CallToolResult::success({Content::text("Hello, " + name + "!")});
            });

        // Tool 2: add -- adds two numbers
        router_.add_tool(
            "add", "Add two numbers",
            json{
                {"type", "object"},
                {"properties",
                 {{"a", {{"type", "number"}, {"description", "First number"}}},
                  {"b", {{"type", "number"}, {"description", "Second number"}}}}},
                {"required", {"a", "b"}}},
            [](ToolCallContext ctx) -> asio::awaitable<CallToolResult> {
                auto args = ctx.arguments.value_or(JsonObject{});
                double a = args.count("a") ? args.at("a").get<double>() : 0.0;
                double b = args.count("b") ? args.at("b").get<double>() : 0.0;
                co_return CallToolResult::success(
                    {Content::text(std::to_string(a + b))});
            });
    }
};

// =============================================================================
// Helper: extract the text from the first Content item in a CallToolResult
// =============================================================================

std::string first_text(const CallToolResult& result) {
    if (result.content.empty()) return "(no content)";
    const auto* text_content = result.content.front().raw().as_text();
    if (text_content) return text_content->text;
    return "(non-text content)";
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::cout << "MCP Simple Client Example\n";
    std::cout << "=========================\n\n";

    asio::io_context io;

    // -------------------------------------------------------------------------
    // Step 1: Create an in-memory transport pair
    // -------------------------------------------------------------------------
    auto [client_transport, server_transport] = make_transport_pair(io.get_executor());

    // -------------------------------------------------------------------------
    // Step 2: Create handlers
    // -------------------------------------------------------------------------
    auto server_handler = std::make_shared<SimpleServerHandler>();
    auto client_handler = std::make_shared<ClientHandler>(); // default (no-op) client handler

    // -------------------------------------------------------------------------
    // Step 3+4: Run both server and client in a single outer coroutine
    // -------------------------------------------------------------------------
    CancellationToken cancel;

    asio::co_spawn(
        io,
        [&]() -> asio::awaitable<void> {
            // Start the server in a nested background coroutine
            asio::co_spawn(
                co_await asio::this_coro::executor,
                [&]() -> asio::awaitable<void> {
                    co_await serve_server(
                        std::move(server_transport), server_handler, cancel);
                },
                asio::detached);

            // 4a. Create client info for the initialization handshake
            InitializeRequestParams client_info;
            client_info.client_info = Implementation{"simple-client-example", "0.1.0"};

            // 4b. Start the client service (performs the initialization handshake)
            std::cout << "Connecting to server...\n";
            auto service =
                co_await serve_client(std::move(client_transport), client_handler, client_info);

            auto peer = service.peer();
            auto& server_info = service.peer_info();
            std::cout << "Connected to server: " << server_info.server_info.name << " "
                      << server_info.server_info.version << "\n";
            std::cout << "Protocol version: " << server_info.protocol_version.value()
                      << "\n\n";

            // -----------------------------------------------------------------
            // 4c. List available tools
            // -----------------------------------------------------------------
            std::cout << "--- Listing tools ---\n";
            auto list_result = co_await peer->send_request(ListToolsRequest{});

            if (auto* resp = std::get_if<ServerResult>(&list_result)) {
                if (resp->is<ListToolsResult>()) {
                    const auto& tools_result = resp->get<ListToolsResult>();
                    std::cout << "Server has " << tools_result.tools.size()
                              << " tool(s):\n";
                    for (const auto& tool : tools_result.tools) {
                        std::cout << "  - " << tool.name;
                        if (tool.description.has_value()) {
                            std::cout << ": " << *tool.description;
                        }
                        std::cout << "\n";
                    }
                } else {
                    std::cout << "Unexpected response type for ListTools\n";
                }
            } else if (auto* err = std::get_if<ErrorData>(&list_result)) {
                std::cout << "Error listing tools: " << err->message << "\n";
            }
            std::cout << "\n";

            // -----------------------------------------------------------------
            // 4d. Call the "greet" tool
            // -----------------------------------------------------------------
            std::cout << "--- Calling 'greet' tool ---\n";
            {
                CallToolRequestParams params;
                params.name = "greet";
                params.arguments = JsonObject{{"name", "MCP"}};

                auto call_result =
                    co_await peer->send_request(CallToolRequest{params});

                if (auto* resp = std::get_if<ServerResult>(&call_result)) {
                    if (resp->is<CallToolResult>()) {
                        const auto& tool_result = resp->get<CallToolResult>();
                        std::cout << "Result: " << first_text(tool_result) << "\n";
                    } else {
                        std::cout << "Unexpected response type for CallTool\n";
                    }
                } else if (auto* err = std::get_if<ErrorData>(&call_result)) {
                    std::cout << "Error: " << err->message << "\n";
                }
            }
            std::cout << "\n";

            // -----------------------------------------------------------------
            // 4e. Call the "add" tool
            // -----------------------------------------------------------------
            std::cout << "--- Calling 'add' tool ---\n";
            {
                CallToolRequestParams params;
                params.name = "add";
                params.arguments = JsonObject{{"a", 17}, {"b", 25}};

                auto call_result =
                    co_await peer->send_request(CallToolRequest{params});

                if (auto* resp = std::get_if<ServerResult>(&call_result)) {
                    if (resp->is<CallToolResult>()) {
                        const auto& tool_result = resp->get<CallToolResult>();
                        std::cout << "17 + 25 = " << first_text(tool_result) << "\n";
                    } else {
                        std::cout << "Unexpected response type for CallTool\n";
                    }
                } else if (auto* err = std::get_if<ErrorData>(&call_result)) {
                    std::cout << "Error: " << err->message << "\n";
                }
            }
            std::cout << "\n";

            // -----------------------------------------------------------------
            // 4f. Call a non-existent tool to demonstrate error handling
            // -----------------------------------------------------------------
            std::cout << "--- Calling non-existent tool ---\n";
            {
                CallToolRequestParams params;
                params.name = "does_not_exist";

                auto call_result =
                    co_await peer->send_request(CallToolRequest{params});

                if (auto* resp = std::get_if<ServerResult>(&call_result)) {
                    if (resp->is<CallToolResult>()) {
                        const auto& tool_result = resp->get<CallToolResult>();
                        bool is_err = tool_result.is_error.value_or(false);
                        std::cout << "Result (is_error="
                                  << (is_err ? "true" : "false")
                                  << "): " << first_text(tool_result) << "\n";
                    } else {
                        std::cout << "Unexpected response type\n";
                    }
                } else if (auto* err = std::get_if<ErrorData>(&call_result)) {
                    std::cout << "Error: " << err->message << "\n";
                }
            }
            std::cout << "\n";

            // -----------------------------------------------------------------
            // 4g. Clean shutdown
            // -----------------------------------------------------------------
            std::cout << "Shutting down...\n";
            service.close();
            cancel.cancel();
            std::cout << "Done.\n";
        },
        asio::detached);

    // Run the event loop. The receive loops don't exit cleanly when
    // cancelled, so we use run_for() with a generous timeout instead of
    // run() to ensure the process terminates.
    io.run_for(std::chrono::seconds(10));

    return 0;
}
