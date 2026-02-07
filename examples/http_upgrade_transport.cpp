/// http_upgrade_transport.cpp — MCP C++ SDK HTTP upgrade transport example
///
/// Demonstrates the HTTP upgrade pattern: client sends an HTTP request with
/// "Upgrade: mcp" header, server responds with 101 Switching Protocols, then
/// both sides switch to line-delimited JSON over the raw TCP socket using
/// AsyncRwTransport (via make_socket_transport).
///
/// This is a demonstration pattern, not a separate transport class.

#ifdef MCP_HTTP_TRANSPORT

#include <iostream>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>

#include "mcp/mcp.hpp"

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;
using namespace mcp;

// =============================================================================
// Server handler
// =============================================================================

class UpgradeServerHandler : public ToolRouterHandler {
public:
    UpgradeServerHandler() {
        router_.add_tool(
            "ping", "Respond with pong",
            json{{"type", "object"}, {"properties", json::object()}},
            [](ToolCallContext ctx) -> asio::awaitable<CallToolResult> {
                co_return CallToolResult::success({Content::text("pong")});
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
// Server: accept HTTP upgrade request
// =============================================================================

/// Read an HTTP request from the socket. If it contains "Upgrade: mcp",
/// respond with 101 Switching Protocols and return the raw socket for
/// line-delimited JSON communication. Otherwise return nullopt.
asio::awaitable<std::optional<tcp::socket>>
handle_upgrade_server(tcp::socket socket) {
    beast::flat_buffer buffer;
    http::request<http::string_body> req;

    co_await http::async_read(socket, buffer, req, asio::use_awaitable);

    // Check for upgrade header
    auto upgrade_it = req.find(http::field::upgrade);
    if (upgrade_it == req.end() || upgrade_it->value() != "mcp") {
        // Not an upgrade request — send 400
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::content_type, "text/plain");
        res.body() = "Expected Upgrade: mcp header";
        res.prepare_payload();
        co_await http::async_write(socket, res, asio::use_awaitable);
        co_return std::nullopt;
    }

    // Respond with 101 Switching Protocols
    http::response<http::empty_body> res{http::status::switching_protocols, req.version()};
    res.set(http::field::upgrade, "mcp");
    res.set(http::field::connection, "Upgrade");
    res.prepare_payload();
    co_await http::async_write(socket, res, asio::use_awaitable);

    // Return the raw socket for MCP communication
    co_return std::move(socket);
}

// =============================================================================
// Client: send HTTP upgrade request
// =============================================================================

/// Connect to the server, send an HTTP upgrade request, and if the server
/// responds with 101, return the raw socket for MCP communication.
asio::awaitable<std::optional<tcp::socket>>
do_upgrade_client(asio::any_io_executor executor,
                  const std::string& host, uint16_t port) {
    tcp::resolver resolver(executor);
    auto endpoints = co_await resolver.async_resolve(
        host, std::to_string(port), asio::use_awaitable);

    tcp::socket socket(executor);
    co_await asio::async_connect(socket, endpoints, asio::use_awaitable);

    // Send HTTP upgrade request
    http::request<http::empty_body> req{http::verb::get, "/mcp", 11};
    req.set(http::field::host, host);
    req.set(http::field::upgrade, "mcp");
    req.set(http::field::connection, "Upgrade");
    req.prepare_payload();

    co_await http::async_write(socket, req, asio::use_awaitable);

    // Read response
    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    co_await http::async_read(socket, buffer, res, asio::use_awaitable);

    if (res.result() != http::status::switching_protocols) {
        std::cerr << "Client: upgrade failed, server returned "
                  << static_cast<int>(res.result()) << "\n";
        co_return std::nullopt;
    }

    co_return std::move(socket);
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::cout << "MCP HTTP Upgrade Transport Example\n";
    std::cout << "====================================\n\n";

    asio::io_context io;
    auto executor = io.get_executor();
    CancellationToken cancel;

    auto server_handler = std::make_shared<UpgradeServerHandler>();

    asio::co_spawn(io, [&]() -> asio::awaitable<void> {
        // --- Server side ---
        tcp::acceptor acceptor(executor, tcp::endpoint(tcp::v4(), 0));
        auto port = acceptor.local_endpoint().port();
        std::cout << "Server listening on port " << port << "\n";

        // Spawn client
        asio::co_spawn(executor, [&, port]() -> asio::awaitable<void> {
            // --- Client side: HTTP upgrade ---
            auto maybe_socket = co_await do_upgrade_client(
                executor, "127.0.0.1", port);

            if (!maybe_socket) {
                std::cerr << "Client: HTTP upgrade failed\n";
                cancel.cancel();
                co_return;
            }

            std::cout << "Client: HTTP upgrade succeeded, switching to MCP\n";

            auto transport = make_socket_transport<RoleClient>(
                std::move(*maybe_socket));
            auto client_handler = std::make_shared<ClientHandler>();

            InitializeRequestParams client_info;
            client_info.client_info = Implementation{"upgrade-client-example", "0.1.0"};

            auto service = co_await serve_client(
                std::move(transport), client_handler, client_info);

            auto peer = service.peer();
            std::cout << "Client: MCP session established\n";

            // List tools
            auto list_result = co_await peer->send_request(ListToolsRequest{});
            if (auto* resp = std::get_if<ServerResult>(&list_result)) {
                if (resp->is<ListToolsResult>()) {
                    const auto& tools = resp->get<ListToolsResult>().tools;
                    std::cout << "Client: server has " << tools.size() << " tool(s)\n";
                }
            }

            // Call ping
            {
                CallToolRequestParams params;
                params.name = "ping";
                auto result = co_await peer->send_request(CallToolRequest{params});
                if (auto* resp = std::get_if<ServerResult>(&result)) {
                    if (resp->is<CallToolResult>()) {
                        std::cout << "Client: ping -> "
                                  << first_text(resp->get<CallToolResult>()) << "\n";
                    }
                }
            }

            std::cout << "Client: done, shutting down.\n";
            service.close();
            cancel.cancel();
        }, asio::detached);

        // Accept one TCP connection
        auto socket = co_await acceptor.async_accept(asio::use_awaitable);
        std::cout << "Server: accepted connection, handling upgrade...\n";

        auto maybe_socket = co_await handle_upgrade_server(std::move(socket));
        if (!maybe_socket) {
            std::cerr << "Server: upgrade handshake failed\n";
            co_return;
        }

        std::cout << "Server: upgrade complete, serving MCP\n";
        auto transport = make_socket_transport<RoleServer>(
            std::move(*maybe_socket));
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
