/// tcp_transport.cpp — MCP C++ SDK TCP transport example
///
/// Demonstrates a TCP server and client communicating via AsyncRwTransport.
/// The server binds a TCP listener, accepts a connection, wraps it with
/// make_socket_transport(), and serves. The client connects and calls tools.

#include <iostream>

#include <boost/asio.hpp>

#include "mcp/mcp.hpp"

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using namespace mcp;

// =============================================================================
// Server handler: calculator with add/multiply tools
// =============================================================================

class TcpServerHandler : public ToolRouterHandler {
public:
    TcpServerHandler() {
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
// Helper: extract text from CallToolResult
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
    std::cout << "MCP TCP Transport Example\n";
    std::cout << "=========================\n\n";

    asio::io_context io;
    auto executor = io.get_executor();
    CancellationToken cancel;

    // Start server in the background
    auto server_handler = std::make_shared<TcpServerHandler>();

    asio::co_spawn(io, [&]() -> asio::awaitable<void> {
        // --- Server side ---
        tcp::acceptor acceptor(executor, tcp::endpoint(tcp::v4(), 0));
        auto port = acceptor.local_endpoint().port();
        std::cout << "Server listening on port " << port << "\n";

        // Spawn client once we know the port
        asio::co_spawn(executor, [&, port]() -> asio::awaitable<void> {
            // --- Client side ---
            tcp::socket socket(executor);
            co_await socket.async_connect(
                tcp::endpoint(asio::ip::make_address("127.0.0.1"), port),
                asio::use_awaitable);

            auto transport = make_socket_transport<RoleClient>(std::move(socket));
            auto client_handler = std::make_shared<ClientHandler>();

            InitializeRequestParams client_info;
            client_info.client_info = Implementation{"tcp-client-example", "0.1.0"};

            std::cout << "Client: connecting...\n";
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

            // Call add(3, 4)
            {
                CallToolRequestParams params;
                params.name = "add";
                params.arguments = JsonObject{{"a", 3}, {"b", 4}};
                auto result = co_await peer->send_request(CallToolRequest{params});
                if (auto* resp = std::get_if<ServerResult>(&result)) {
                    if (resp->is<CallToolResult>()) {
                        std::cout << "Client: 3 + 4 = "
                                  << first_text(resp->get<CallToolResult>()) << "\n";
                    }
                }
            }

            // Call multiply(6, 7)
            {
                CallToolRequestParams params;
                params.name = "multiply";
                params.arguments = JsonObject{{"a", 6}, {"b", 7}};
                auto result = co_await peer->send_request(CallToolRequest{params});
                if (auto* resp = std::get_if<ServerResult>(&result)) {
                    if (resp->is<CallToolResult>()) {
                        std::cout << "Client: 6 * 7 = "
                                  << first_text(resp->get<CallToolResult>()) << "\n";
                    }
                }
            }

            std::cout << "Client: done, shutting down.\n";
            service.close();
            cancel.cancel();
        }, asio::detached);

        // Accept one connection
        auto socket = co_await acceptor.async_accept(asio::use_awaitable);
        std::cout << "Server: accepted connection\n";

        auto transport = make_socket_transport<RoleServer>(std::move(socket));
        co_await serve_server(std::move(transport), server_handler, cancel);
    }, asio::detached);

    io.run_for(std::chrono::seconds(10));
    std::cout << "Done.\n";
    return 0;
}
