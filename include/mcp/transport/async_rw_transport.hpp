#pragma once

#include <boost/asio.hpp>

#include "mcp/transport/transport.hpp"

namespace mcp {

namespace asio = boost::asio;

/// Generic transport wrapper for any pair of Asio AsyncReadStream/AsyncWriteStream.
///
/// Uses line-delimited JSON framing (same as stdio transport).
/// This can wrap pipes, Unix domain sockets, TCP sockets, etc.
///
/// Template parameters:
/// - R: service role (RoleClient or RoleServer)
/// - ReadStream: Asio AsyncReadStream type
/// - WriteStream: Asio AsyncWriteStream type
template <typename R, typename ReadStream, typename WriteStream>
class AsyncRwTransport : public Transport<R> {
public:
    AsyncRwTransport(ReadStream reader, WriteStream writer)
        : reader_(std::move(reader)), writer_(std::move(writer)) {}

    ~AsyncRwTransport() override = default;

    asio::awaitable<void> send(TxJsonRpcMessage<R> msg) override;
    asio::awaitable<std::optional<RxJsonRpcMessage<R>>> receive() override;
    asio::awaitable<void> close() override;

private:
    ReadStream reader_;
    WriteStream writer_;
    asio::streambuf read_buf_;
    bool closed_ = false;
};

/// Helper: create a transport from a pair of file descriptors (pipes).
template <typename R>
std::unique_ptr<Transport<R>> make_pipe_transport(
    asio::any_io_executor executor,
    int read_fd,
    int write_fd);

/// Helper: create a transport from a connected socket.
template <typename R>
std::unique_ptr<Transport<R>> make_socket_transport(
    asio::ip::tcp::socket socket);

} // namespace mcp
