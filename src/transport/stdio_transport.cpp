#include "mcp/transport/stdio_transport.hpp"
#include "mcp/service/service_role.hpp"

#include <unistd.h>

#include <spdlog/spdlog.h>

namespace mcp {

template <typename R>
StdioTransport<R>::StdioTransport(asio::any_io_executor executor)
    : stdin_(executor, ::dup(STDIN_FILENO))
    , stdout_(executor, ::dup(STDOUT_FILENO)) {}

template <typename R>
asio::awaitable<void> StdioTransport<R>::send(TxJsonRpcMessage<R> msg) {
    if (closed_) co_return;
    json j = msg;
    std::string line = j.dump() + "\n";
    co_await asio::async_write(stdout_, asio::buffer(line), asio::use_awaitable);
}

template <typename R>
asio::awaitable<std::optional<RxJsonRpcMessage<R>>> StdioTransport<R>::receive() {
    if (closed_) co_return std::nullopt;
    boost::system::error_code ec;
    auto n = co_await asio::async_read_until(
        stdin_, read_buf_, '\n',
        asio::redirect_error(asio::use_awaitable, ec));
    if (ec) {
        closed_ = true;
        co_return std::nullopt;
    }
    std::istream is(&read_buf_);
    std::string line;
    std::getline(is, line);
    if (line.empty()) co_return std::nullopt;
    try {
        auto j = json::parse(line);
        co_return j.get<RxJsonRpcMessage<R>>();
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse JSON-RPC message: {}", e.what());
        co_return std::nullopt;
    }
}

template <typename R>
asio::awaitable<void> StdioTransport<R>::close() {
    closed_ = true;
    boost::system::error_code ec;
    stdin_.close(ec);
    stdout_.close(ec);
    co_return;
}

// Explicit instantiations
template class StdioTransport<RoleClient>;
template class StdioTransport<RoleServer>;

} // namespace mcp
