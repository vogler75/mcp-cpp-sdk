#pragma once

#ifdef MCP_HTTP_TRANSPORT

#include <string>
#include <optional>
#include <memory>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

#include "mcp/transport/transport.hpp"
#include "mcp/service/service_role.hpp"

namespace mcp {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using json = nlohmann::json;

/// WebSocket transport using Boost.Beast.
///
/// Unlike AsyncRwTransport (line-delimited JSON), WebSocket uses message
/// framing — each WebSocket text message is one complete JSON-RPC message.
///
/// Template parameter R is the service role (RoleClient or RoleServer).
template <typename R>
class WebSocketTransport : public Transport<R> {
public:
    /// Construct from an already-connected and handshaken WebSocket stream.
    explicit WebSocketTransport(
        websocket::stream<beast::tcp_stream> ws)
        : ws_(std::move(ws)) {}

    ~WebSocketTransport() override = default;

    asio::awaitable<void> send(TxJsonRpcMessage<R> msg) override;
    asio::awaitable<std::optional<RxJsonRpcMessage<R>>> receive() override;
    asio::awaitable<void> close() override;

private:
    websocket::stream<beast::tcp_stream> ws_;
    bool closed_ = false;
};

// =============================================================================
// Client helper: connect to a WebSocket server
// =============================================================================

/// Create a WebSocket client transport by connecting to the given host:port/path
/// and performing the WebSocket handshake.
template <typename R>
asio::awaitable<std::unique_ptr<Transport<R>>>
make_websocket_client_transport(
    asio::any_io_executor executor,
    const std::string& host,
    const std::string& port,
    const std::string& path = "/");

// =============================================================================
// Server helper: accept a WebSocket connection from a raw TCP socket
// =============================================================================

/// Accept a WebSocket connection on an already-connected TCP socket by
/// performing the server-side WebSocket handshake.
template <typename R>
asio::awaitable<std::unique_ptr<Transport<R>>>
accept_websocket_transport(tcp::socket socket);

} // namespace mcp

#endif // MCP_HTTP_TRANSPORT
