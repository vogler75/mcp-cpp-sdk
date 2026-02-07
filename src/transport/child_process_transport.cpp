#include "mcp/transport/child_process_transport.hpp"
#include "mcp/service/service_role.hpp"

#include <spdlog/spdlog.h>

namespace mcp {

// TODO: Implement constructor - spawns child process using boost::process::v2
// and sets up pipe-based communication with child_stdin_ / child_stdout_.
template <typename R>
ChildProcessTransport<R>::ChildProcessTransport(
    asio::any_io_executor executor, Options options)
    : child_(nullptr)
    , child_stdin_(executor, -1)
    , child_stdout_(executor, -1) {
    // TODO: Use boost::process::v2 to spawn the child process.
    // Steps:
    //   1. Create pipes for stdin/stdout
    //   2. Spawn child with options.program, options.args, options.env
    //   3. Store the process handle in child_
    //   4. Wrap pipe fds in child_stdin_ and child_stdout_
    spdlog::warn(
        "ChildProcessTransport: not yet implemented, program='{}'",
        options.program);
}

template <typename R>
ChildProcessTransport<R>::~ChildProcessTransport() {
    // TODO: Terminate and wait for child process if still running.
}

template <typename R>
asio::awaitable<void> ChildProcessTransport<R>::send(TxJsonRpcMessage<R> msg) {
    // TODO: Serialize msg to JSON, append newline, write to child_stdin_.
    if (closed_) co_return;
    json j = msg;
    std::string line = j.dump() + "\n";
    co_await asio::async_write(child_stdin_, asio::buffer(line), asio::use_awaitable);
}

template <typename R>
asio::awaitable<std::optional<RxJsonRpcMessage<R>>>
ChildProcessTransport<R>::receive() {
    // TODO: Read a line from child_stdout_, parse as JSON-RPC.
    if (closed_) co_return std::nullopt;
    boost::system::error_code ec;
    co_await asio::async_read_until(
        child_stdout_, read_buf_, '\n',
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
        spdlog::error("ChildProcessTransport: failed to parse JSON-RPC message: {}", e.what());
        co_return std::nullopt;
    }
}

template <typename R>
asio::awaitable<void> ChildProcessTransport<R>::close() {
    closed_ = true;
    boost::system::error_code ec;
    child_stdin_.close(ec);
    child_stdout_.close(ec);
    // TODO: Terminate child process if still running.
    co_return;
}

template <typename R>
int ChildProcessTransport<R>::pid() const {
    // TODO: Return the child process ID from child_.
    return -1;
}

// Explicit instantiations
template class ChildProcessTransport<RoleClient>;
template class ChildProcessTransport<RoleServer>;

} // namespace mcp
