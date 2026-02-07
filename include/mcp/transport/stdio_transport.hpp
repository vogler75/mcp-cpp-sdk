#pragma once

#include <string>

#include <boost/asio.hpp>

#include "mcp/transport/transport.hpp"

namespace mcp {

namespace asio = boost::asio;

/// Line-delimited JSON transport over stdin/stdout.
///
/// Each JSON-RPC message is sent as a single line of JSON followed by a newline.
/// This is the standard MCP stdio transport.
///
/// Template parameter R is the service role (RoleClient or RoleServer).
template <typename R>
class StdioTransport : public Transport<R> {
public:
    /// Construct a stdio transport using the given executor.
    /// Reads from stdin, writes to stdout.
    explicit StdioTransport(asio::any_io_executor executor);

    ~StdioTransport() override = default;

    asio::awaitable<void> send(TxJsonRpcMessage<R> msg) override;
    asio::awaitable<std::optional<RxJsonRpcMessage<R>>> receive() override;
    asio::awaitable<void> close() override;

private:
    asio::posix::stream_descriptor stdin_;
    asio::posix::stream_descriptor stdout_;
    asio::streambuf read_buf_;
    bool closed_ = false;
};

} // namespace mcp
