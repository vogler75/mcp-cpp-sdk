#if defined(__unix__) || defined(__APPLE__)

#include "mcp/transport/child_process_transport.hpp"
#include "mcp/service/service_role.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>

#include <spdlog/spdlog.h>

namespace mcp {

template <typename R>
ChildProcessTransport<R>::ChildProcessTransport(
    asio::any_io_executor executor, Options options)
    : child_pid_(-1)
    , child_stdin_(executor)
    , child_stdout_(executor) {
    // Create pipes:
    //   stdin_pipe:  parent writes to [1], child reads from [0]
    //   stdout_pipe: child writes to [1], parent reads from [0]
    int stdin_pipe[2];
    int stdout_pipe[2];

    if (::pipe(stdin_pipe) == -1) {
        throw std::system_error(errno, std::system_category(),
            "ChildProcessTransport: failed to create stdin pipe");
    }
    if (::pipe(stdout_pipe) == -1) {
        ::close(stdin_pipe[0]);
        ::close(stdin_pipe[1]);
        throw std::system_error(errno, std::system_category(),
            "ChildProcessTransport: failed to create stdout pipe");
    }

    pid_t pid = ::fork();
    if (pid == -1) {
        // Fork failed — close all pipe FDs
        ::close(stdin_pipe[0]);
        ::close(stdin_pipe[1]);
        ::close(stdout_pipe[0]);
        ::close(stdout_pipe[1]);
        throw std::system_error(errno, std::system_category(),
            "ChildProcessTransport: fork failed");
    }

    if (pid == 0) {
        // === Child process ===

        // Redirect stdin to read end of stdin_pipe
        ::dup2(stdin_pipe[0], STDIN_FILENO);
        // Redirect stdout to write end of stdout_pipe
        ::dup2(stdout_pipe[1], STDOUT_FILENO);

        // Close all pipe FDs (they are now duplicated to stdin/stdout)
        ::close(stdin_pipe[0]);
        ::close(stdin_pipe[1]);
        ::close(stdout_pipe[0]);
        ::close(stdout_pipe[1]);

        // Set environment variables
        for (const auto& [key, value] : options.env) {
            ::setenv(key.c_str(), value.c_str(), 1);
        }

        // Build argv for execvp
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(options.program.c_str()));
        for (const auto& arg : options.args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        ::execvp(options.program.c_str(), argv.data());

        // If execvp returns, it failed
        std::fprintf(stderr, "ChildProcessTransport: execvp failed for '%s': %s\n",
            options.program.c_str(), std::strerror(errno));
        ::_exit(127);
    }

    // === Parent process ===
    child_pid_ = pid;

    // Close the child-side ends of the pipes
    ::close(stdin_pipe[0]);   // child reads from this
    ::close(stdout_pipe[1]);  // child writes to this

    // Wrap the parent-side pipe FDs in Asio stream descriptors
    child_stdin_.assign(stdin_pipe[1]);   // parent writes to child's stdin
    child_stdout_.assign(stdout_pipe[0]); // parent reads from child's stdout

    spdlog::info(
        "ChildProcessTransport: spawned '{}' with pid {}",
        options.program, child_pid_);
}

template <typename R>
ChildProcessTransport<R>::~ChildProcessTransport() {
    if (child_pid_ <= 0) return;

    // Close pipe FDs so the child sees EOF
    boost::system::error_code ec;
    if (child_stdin_.is_open()) child_stdin_.close(ec);
    if (child_stdout_.is_open()) child_stdout_.close(ec);

    // Try SIGTERM first
    if (::kill(child_pid_, SIGTERM) == 0) {
        // Wait up to 2 seconds for graceful exit
        int status = 0;
        for (int i = 0; i < 20; ++i) {
            pid_t result = ::waitpid(child_pid_, &status, WNOHANG);
            if (result == child_pid_) {
                spdlog::debug("ChildProcessTransport: child {} exited with status {}",
                    child_pid_, WEXITSTATUS(status));
                child_pid_ = -1;
                return;
            }
            if (result == -1) {
                child_pid_ = -1;
                return;
            }
            // Sleep 100ms
            ::usleep(100000);
        }

        // Still running — force kill
        spdlog::warn("ChildProcessTransport: child {} did not exit after SIGTERM, sending SIGKILL",
            child_pid_);
        ::kill(child_pid_, SIGKILL);
        ::waitpid(child_pid_, &status, 0);
    }

    child_pid_ = -1;
}

template <typename R>
asio::awaitable<void> ChildProcessTransport<R>::send(TxJsonRpcMessage<R> msg) {
    if (closed_) co_return;
    json j = msg;
    std::string line = j.dump() + "\n";
    co_await asio::async_write(child_stdin_, asio::buffer(line), asio::use_awaitable);
}

template <typename R>
asio::awaitable<std::optional<RxJsonRpcMessage<R>>>
ChildProcessTransport<R>::receive() {
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
    if (closed_) co_return;
    closed_ = true;
    boost::system::error_code ec;
    child_stdin_.close(ec);
    child_stdout_.close(ec);

    // Send SIGTERM to child and reap
    if (child_pid_ > 0) {
        ::kill(child_pid_, SIGTERM);
        int status = 0;
        ::waitpid(child_pid_, &status, WNOHANG);
    }

    co_return;
}

template <typename R>
int ChildProcessTransport<R>::pid() const {
    return static_cast<int>(child_pid_);
}

// Explicit instantiations
template class ChildProcessTransport<RoleClient>;
template class ChildProcessTransport<RoleServer>;

} // namespace mcp

#endif // defined(__unix__) || defined(__APPLE__)
