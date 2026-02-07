/// unix_socket_transport.cpp — MCP C++ SDK Unix domain socket transport example
///
/// Demonstrates a Unix domain socket server and client using AsyncRwTransport.
/// The server binds a local::stream_protocol acceptor, accepts a connection,
/// wraps it via make_unix_socket_transport(), and serves MCP.

#include <iostream>
#include <cstdio>

#include <boost/asio.hpp>
#include <boost/asio/local/stream_protocol.hpp>

#include "mcp/mcp.hpp"

namespace asio = boost::asio;
using namespace mcp;
using unix_proto = asio::local::stream_protocol;

// =============================================================================
// Server handler
// =============================================================================

class UnixServerHandler : public ToolRouterHandler {
public:
    UnixServerHandler() {
        router_.add_tool(
            "greet", "Greet someone by name",
            json{{"type", "object"},
                 {"properties", {{"name", {{"type", "string"}}}}},
                 {"required", {"name"}}},
            [](ToolCallContext ctx) -> asio::awaitable<CallToolResult> {
                auto args = ctx.arguments.value_or(JsonObject{});
                std::string name = args.count("name")
                    ? args.at("name").get<std::string>() : "World";
                co_return CallToolResult::success(
                    {Content::text("Hello from Unix socket, " + name + "!")});
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
    std::cout << "MCP Unix Socket Transport Example\n";
    std::cout << "==================================\n\n";

    const std::string socket_path = "/tmp/mcp-cpp-example.sock";

    // Clean up any leftover socket file
    std::remove(socket_path.c_str());

    asio::io_context io;
    auto executor = io.get_executor();
    CancellationToken cancel;

    auto server_handler = std::make_shared<UnixServerHandler>();

    asio::co_spawn(io, [&]() -> asio::awaitable<void> {
        // --- Server side ---
        unix_proto::acceptor acceptor(executor,
            unix_proto::endpoint(socket_path));
        std::cout << "Server listening on " << socket_path << "\n";

        // Spawn client
        asio::co_spawn(executor, [&]() -> asio::awaitable<void> {
            // --- Client side ---
            unix_proto::socket socket(executor);
            co_await socket.async_connect(
                unix_proto::endpoint(socket_path), asio::use_awaitable);

            auto transport = make_unix_socket_transport<RoleClient>(std::move(socket));
            auto client_handler = std::make_shared<ClientHandler>();

            InitializeRequestParams client_info;
            client_info.client_info = Implementation{"unix-client-example", "0.1.0"};

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
                }
            }

            // Call greet
            {
                CallToolRequestParams params;
                params.name = "greet";
                params.arguments = JsonObject{{"name", "MCP"}};
                auto result = co_await peer->send_request(CallToolRequest{params});
                if (auto* resp = std::get_if<ServerResult>(&result)) {
                    if (resp->is<CallToolResult>()) {
                        std::cout << "Client: "
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

        auto transport = make_unix_socket_transport<RoleServer>(std::move(socket));
        co_await serve_server(std::move(transport), server_handler, cancel);
    }, asio::detached);

    io.run_for(std::chrono::seconds(10));

    // Clean up socket file
    std::remove(socket_path.c_str());

    std::cout << "Done.\n";
    return 0;
}
