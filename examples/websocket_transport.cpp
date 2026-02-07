/// websocket_transport.cpp — MCP C++ SDK WebSocket transport example
///
/// Demonstrates a WebSocket server and client. The server listens for TCP
/// connections, performs WebSocket handshake, then serves MCP. The client
/// connects, handshakes, and calls tools.

#ifdef MCP_HTTP_TRANSPORT

#include <iostream>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>

#include "mcp/mcp.hpp"
#include "mcp/transport/websocket_transport.hpp"

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using namespace mcp;

// =============================================================================
// Server handler: calculator
// =============================================================================

class WsServerHandler : public ToolRouterHandler {
public:
    WsServerHandler() {
        router_.add_tool(
            "add", "Add two numbers",
            json{{"type", "object"},
                 {"properties", {{"a", {{"type", "number"}}}, {"b", {{"type", "number"}}}}},
                 {"required", {"a", "b"}}},
            [](ToolCallContext ctx) -> asio::awaitable<CallToolResult> {
                auto args = ctx.arguments.value_or(JsonObject{});
                double a = args.count("a") ? args.at("a").get<double>() : 0.0;
                double b = args.count("b") ? args.at("b").get<double>() : 0.0;
                co_return CallToolResult::success({Content::text(std::to_string(a + b))});
            });

        router_.add_tool(
            "multiply", "Multiply two numbers",
            json{{"type", "object"},
                 {"properties", {{"a", {{"type", "number"}}}, {"b", {{"type", "number"}}}}},
                 {"required", {"a", "b"}}},
            [](ToolCallContext ctx) -> asio::awaitable<CallToolResult> {
                auto args = ctx.arguments.value_or(JsonObject{});
                double a = args.count("a") ? args.at("a").get<double>() : 0.0;
                double b = args.count("b") ? args.at("b").get<double>() : 0.0;
                co_return CallToolResult::success({Content::text(std::to_string(a * b))});
            });
    }
};

// =============================================================================
// Helper
// =============================================================================

std::string first_text(const CallToolResult& result) {
    if (result.content.empty()) return "(no content)";
    const auto* tc = result.content.front().raw().as_text();
    return tc ? tc->text : "(non-text content)";
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::cout << "MCP WebSocket Transport Example\n";
    std::cout << "================================\n\n";

    asio::io_context io;
    auto executor = io.get_executor();
    CancellationToken cancel;

    auto server_handler = std::make_shared<WsServerHandler>();

    asio::co_spawn(io, [&]() -> asio::awaitable<void> {
        // --- Server side: listen for TCP, then accept WebSocket ---
        tcp::acceptor acceptor(executor, tcp::endpoint(tcp::v4(), 0));
        auto port = acceptor.local_endpoint().port();
        std::cout << "Server listening on port " << port << "\n";

        // Spawn client
        asio::co_spawn(executor, [&, port]() -> asio::awaitable<void> {
            // --- Client side: WebSocket connect ---
            auto transport = co_await make_websocket_client_transport<RoleClient>(
                executor, "127.0.0.1", std::to_string(port), "/");

            auto client_handler = std::make_shared<ClientHandler>();

            InitializeRequestParams client_info;
            client_info.client_info = Implementation{"ws-client-example", "0.1.0"};

            std::cout << "Client: connecting via WebSocket...\n";
            auto service = co_await serve_client(
                std::move(transport), client_handler, client_info);

            auto peer = service.peer();
            std::cout << "Client: connected!\n";

            // List tools
            auto list_result = co_await peer->send_request(ListToolsRequest{});
            if (auto* resp = std::get_if<ServerResult>(&list_result)) {
                if (resp->is<ListToolsResult>()) {
                    const auto& tools = resp->get<ListToolsResult>().tools;
                    std::cout << "Client: server has " << tools.size() << " tool(s)\n";
                    for (const auto& t : tools) {
                        std::cout << "  - " << t.name << "\n";
                    }
                }
            }

            // Call add(10, 20)
            {
                CallToolRequestParams params;
                params.name = "add";
                params.arguments = JsonObject{{"a", 10}, {"b", 20}};
                auto result = co_await peer->send_request(CallToolRequest{params});
                if (auto* resp = std::get_if<ServerResult>(&result)) {
                    if (resp->is<CallToolResult>()) {
                        std::cout << "Client: 10 + 20 = "
                                  << first_text(resp->get<CallToolResult>()) << "\n";
                    }
                }
            }

            // Call multiply(5, 9)
            {
                CallToolRequestParams params;
                params.name = "multiply";
                params.arguments = JsonObject{{"a", 5}, {"b", 9}};
                auto result = co_await peer->send_request(CallToolRequest{params});
                if (auto* resp = std::get_if<ServerResult>(&result)) {
                    if (resp->is<CallToolResult>()) {
                        std::cout << "Client: 5 * 9 = "
                                  << first_text(resp->get<CallToolResult>()) << "\n";
                    }
                }
            }

            std::cout << "Client: done, shutting down.\n";
            service.close();
            cancel.cancel();
        }, asio::detached);

        // Accept one TCP connection, then upgrade to WebSocket
        auto socket = co_await acceptor.async_accept(asio::use_awaitable);
        std::cout << "Server: accepted TCP connection, upgrading to WebSocket...\n";

        auto transport = co_await accept_websocket_transport<RoleServer>(
            std::move(socket));
        std::cout << "Server: WebSocket handshake complete\n";

        co_await serve_server(std::move(transport), server_handler, cancel);
    }, asio::detached);

    io.run_for(std::chrono::seconds(10));
    std::cout << "Done.\n";
    return 0;
}

#else

#include <iostream>

int main() {
    std::cerr << "This example requires MCP_HTTP_TRANSPORT to be enabled.\n";
    std::cerr << "Rebuild with: cmake -DMCP_BUILD_HTTP_TRANSPORT=ON ..\n";
    return 1;
}

#endif // MCP_HTTP_TRANSPORT
