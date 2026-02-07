#pragma once

#include <boost/asio.hpp>
#include <boost/asio/local/stream_protocol.hpp>

#include <iostream>

#include <nlohmann/json.hpp>

#include "mcp/transport/transport.hpp"

namespace mcp {

namespace asio = boost::asio;
using json = nlohmann::json;

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

    asio::awaitable<void> send(TxJsonRpcMessage<R> msg) override {
        if (closed_) co_return;
        json j = msg;
        std::string line = j.dump() + "\n";
        co_await asio::async_write(writer_, asio::buffer(line), asio::use_awaitable);
    }

    asio::awaitable<std::optional<RxJsonRpcMessage<R>>> receive() override {
        if (closed_) co_return std::nullopt;
        boost::system::error_code ec;
        co_await asio::async_read_until(
            reader_, read_buf_, '\n',
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
            co_return j.template get<RxJsonRpcMessage<R>>();
        } catch (const std::exception&) {
            // Failed to parse JSON-RPC message; skip it
            co_return std::nullopt;
        }
    }

    asio::awaitable<void> close() override {
        closed_ = true;
        boost::system::error_code ec;
        // Close both streams. Even when ReadStream == WriteStream (e.g.
        // two posix::stream_descriptors), they wrap distinct file
        // descriptors (via dup()), so both must be closed.
        reader_.close(ec);
        writer_.close(ec);
        co_return;
    }

private:
    ReadStream reader_;
    WriteStream writer_;
    asio::streambuf read_buf_;
    bool closed_ = false;
};

// =============================================================================
// TCP socket transport convenience types and factories
// =============================================================================

/// A TCP socket transport — both read and write use the same socket.
template <typename R>
using TcpTransport = AsyncRwTransport<R,
    asio::ip::tcp::socket, asio::ip::tcp::socket>;

/// Helper: create a transport from a connected TCP socket.
/// The socket is moved into the transport; a single socket is used for both
/// reading and writing, achieved via Asio's internal dup.
template <typename R>
std::unique_ptr<Transport<R>> make_socket_transport(
    asio::ip::tcp::socket socket) {
    // We need two "views" of the same socket. Asio tcp::socket is movable
    // but not copyable. We use the native handle to create a second socket
    // on the same fd. The cleaner approach: use a shared_ptr wrapper or
    // split the socket. Since AsyncRwTransport expects two separate objects,
    // we dup the native handle.
    auto executor = socket.get_executor();
    int fd = socket.native_handle();
    int fd2 = ::dup(fd);
    socket.release();  // release ownership of original fd

    asio::ip::tcp::socket read_sock(executor);
    read_sock.assign(asio::ip::tcp::v4(), fd);

    asio::ip::tcp::socket write_sock(executor);
    write_sock.assign(asio::ip::tcp::v4(), fd2);

    return std::make_unique<AsyncRwTransport<R,
        asio::ip::tcp::socket, asio::ip::tcp::socket>>(
        std::move(read_sock), std::move(write_sock));
}

// =============================================================================
// Unix domain socket transport convenience types and factories
// =============================================================================

#if defined(__unix__) || defined(__APPLE__)

using unix_socket = asio::local::stream_protocol::socket;

/// A Unix domain socket transport.
template <typename R>
using UnixTransport = AsyncRwTransport<R, unix_socket, unix_socket>;

/// Helper: create a transport from a connected Unix domain socket.
template <typename R>
std::unique_ptr<Transport<R>> make_unix_socket_transport(
    asio::local::stream_protocol::socket socket) {
    auto executor = socket.get_executor();
    int fd = socket.native_handle();
    int fd2 = ::dup(fd);
    socket.release();

    asio::local::stream_protocol::socket read_sock(executor);
    read_sock.assign(asio::local::stream_protocol(), fd);

    asio::local::stream_protocol::socket write_sock(executor);
    write_sock.assign(asio::local::stream_protocol(), fd2);

    return std::make_unique<AsyncRwTransport<R,
        unix_socket, unix_socket>>(
        std::move(read_sock), std::move(write_sock));
}

#endif // defined(__unix__) || defined(__APPLE__)

// =============================================================================
// Pipe transport factory
// =============================================================================

/// Helper: create a transport from a pair of file descriptors (pipes).
template <typename R>
std::unique_ptr<Transport<R>> make_pipe_transport(
    asio::any_io_executor executor,
    int read_fd,
    int write_fd) {
    asio::posix::stream_descriptor reader(executor, read_fd);
    asio::posix::stream_descriptor writer(executor, write_fd);
    return std::make_unique<AsyncRwTransport<R,
        asio::posix::stream_descriptor, asio::posix::stream_descriptor>>(
        std::move(reader), std::move(writer));
}

} // namespace mcp
