#pragma once

#include <memory>
#include <string>
#include <vector>

#include "mcp/transport/transport.hpp"
#include "mcp/transport/stdio_transport.hpp"

#ifdef MCP_CHILD_PROCESS_TRANSPORT
#include "mcp/transport/child_process_transport.hpp"
#endif

namespace mcp {

/// Factory helpers for creating transports from various types.
///
/// Similar to Rust's IntoTransport trait — provides convenience functions
/// to convert compatible types into Transport instances.

/// Create a transport that communicates over stdin/stdout.
template <typename R>
TransportPtr<R> into_transport_stdio(asio::any_io_executor executor) {
    return std::make_unique<StdioTransport<R>>(std::move(executor));
}

/// Create a transport that spawns a child process and communicates
/// via its stdin/stdout.
///
/// Requires MCP_BUILD_CHILD_PROCESS to be enabled at build time.
#ifdef MCP_CHILD_PROCESS_TRANSPORT
template <typename R>
TransportPtr<R> into_transport_child(
    asio::any_io_executor executor,
    const std::string& program,
    const std::vector<std::string>& args = {}) {
    typename ChildProcessTransport<R>::Options opts;
    opts.program = program;
    opts.args = args;
    return std::make_unique<ChildProcessTransport<R>>(
        std::move(executor), std::move(opts));
}
#endif

} // namespace mcp
