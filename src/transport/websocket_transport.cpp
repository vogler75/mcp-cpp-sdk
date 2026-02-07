#ifdef MCP_HTTP_TRANSPORT

#include "mcp/transport/websocket_transport.hpp"
#include "mcp/service/service_role.hpp"

#include <spdlog/spdlog.h>

namespace mcp {

// =============================================================================
// WebSocketTransport: send / receive / close
// =============================================================================

template <typename R>
asio::awaitable<void> WebSocketTransport<R>::send(TxJsonRpcMessage<R> msg) {
    if (closed_) co_return;

    json j = msg;
    std::string text = j.dump();

    ws_.text(true);
    co_await ws_.async_write(asio::buffer(text), asio::use_awaitable);
}

template <typename R>
asio::awaitable<std::optional<RxJsonRpcMessage<R>>>
WebSocketTransport<R>::receive() {
    if (closed_) co_return std::nullopt;

    beast::flat_buffer buffer;
    boost::system::error_code ec;

    co_await ws_.async_read(buffer,
        asio::redirect_error(asio::use_awaitable, ec));

    if (ec) {
        if (ec == websocket::error::closed) {
            spdlog::debug("WebSocketTransport: connection closed by peer");
        } else {
            spdlog::error("WebSocketTransport: read error: {}", ec.message());
        }
        closed_ = true;
        co_return std::nullopt;
    }

    std::string text = beast::buffers_to_string(buffer.data());
    if (text.empty()) co_return std::nullopt;

    try {
        auto j = json::parse(text);
        co_return j.get<RxJsonRpcMessage<R>>();
    } catch (const std::exception& e) {
        spdlog::error("WebSocketTransport: failed to parse JSON-RPC message: {}",
            e.what());
        co_return std::nullopt;
    }
}

template <typename R>
asio::awaitable<void> WebSocketTransport<R>::close() {
    if (closed_) co_return;
    closed_ = true;

    boost::system::error_code ec;
    co_await ws_.async_close(websocket::close_code::normal,
        asio::redirect_error(asio::use_awaitable, ec));

    if (ec && ec != websocket::error::closed) {
        spdlog::warn("WebSocketTransport: close error: {}", ec.message());
    }

    co_return;
}

// =============================================================================
// Client helper: connect + handshake
// =============================================================================

template <typename R>
asio::awaitable<std::unique_ptr<Transport<R>>>
make_websocket_client_transport(
    asio::any_io_executor executor,
    const std::string& host,
    const std::string& port,
    const std::string& path) {

    // Resolve
    tcp::resolver resolver(executor);
    auto endpoints = co_await resolver.async_resolve(host, port, asio::use_awaitable);

    // Create WebSocket stream
    websocket::stream<beast::tcp_stream> ws(executor);

    // Connect
    auto ep = co_await beast::get_lowest_layer(ws).async_connect(
        endpoints, asio::use_awaitable);

    // Build the host string for the handshake (host:port)
    std::string host_header = host + ":" + std::to_string(ep.port());

    // Set a timeout for the handshake
    beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(30));

    // WebSocket handshake
    co_await ws.async_handshake(host_header, path, asio::use_awaitable);

    // Disable the timeout after handshake — we use our own read timeouts
    beast::get_lowest_layer(ws).expires_never();

    spdlog::info("WebSocketTransport: connected to ws://{}:{}{}", host, port, path);

    co_return std::make_unique<WebSocketTransport<R>>(std::move(ws));
}

// =============================================================================
// Server helper: accept handshake on existing socket
// =============================================================================

template <typename R>
asio::awaitable<std::unique_ptr<Transport<R>>>
accept_websocket_transport(tcp::socket socket) {

    websocket::stream<beast::tcp_stream> ws(std::move(socket));

    // Set a timeout for the handshake
    beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(30));

    // Accept the WebSocket handshake
    co_await ws.async_accept(asio::use_awaitable);

    // Disable the timeout after handshake
    beast::get_lowest_layer(ws).expires_never();

    spdlog::debug("WebSocketTransport: accepted WebSocket connection");

    co_return std::make_unique<WebSocketTransport<R>>(std::move(ws));
}

// =============================================================================
// Explicit template instantiations
// =============================================================================

template class WebSocketTransport<RoleClient>;
template class WebSocketTransport<RoleServer>;

template asio::awaitable<std::unique_ptr<Transport<RoleClient>>>
make_websocket_client_transport<RoleClient>(
    asio::any_io_executor, const std::string&, const std::string&, const std::string&);

template asio::awaitable<std::unique_ptr<Transport<RoleServer>>>
make_websocket_client_transport<RoleServer>(
    asio::any_io_executor, const std::string&, const std::string&, const std::string&);

template asio::awaitable<std::unique_ptr<Transport<RoleClient>>>
accept_websocket_transport<RoleClient>(tcp::socket);

template asio::awaitable<std::unique_ptr<Transport<RoleServer>>>
accept_websocket_transport<RoleServer>(tcp::socket);

} // namespace mcp

#endif // MCP_HTTP_TRANSPORT
