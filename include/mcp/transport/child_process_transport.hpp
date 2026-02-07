#pragma once

#include <string>
#include <vector>

#include <boost/asio.hpp>
#include <boost/process.hpp>

#include "mcp/transport/transport.hpp"

namespace mcp {

namespace asio = boost::asio;
namespace process = boost::process;

/// Transport that spawns a child process and communicates via its stdin/stdout.
///
/// The child process is started with the given program and arguments.
/// Messages are sent to the child's stdin and received from its stdout
/// using line-delimited JSON framing.
///
/// Template parameter R is the service role (RoleClient or RoleServer).
template <typename R>
class ChildProcessTransport : public Transport<R> {
public:
    struct Options {
        std::string program;
        std::vector<std::string> args;
        std::vector<std::pair<std::string, std::string>> env;
    };

    /// Spawn a child process and create a transport for communicating with it.
    explicit ChildProcessTransport(asio::any_io_executor executor, Options options);

    ~ChildProcessTransport() override;

    asio::awaitable<void> send(TxJsonRpcMessage<R> msg) override;
    asio::awaitable<std::optional<RxJsonRpcMessage<R>>> receive() override;
    asio::awaitable<void> close() override;

    /// Get the child process ID
    int pid() const;

private:
    std::unique_ptr<process::process> child_;
    asio::posix::stream_descriptor child_stdin_;
    asio::posix::stream_descriptor child_stdout_;
    asio::streambuf read_buf_;
    bool closed_ = false;
};

} // namespace mcp
