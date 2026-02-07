#pragma once

#include <memory>

#include "mcp/transport/transport.hpp"

namespace mcp {

/// Factory helpers for creating transports from various types.
///
/// Similar to Rust's IntoTransport trait — provides convenience functions
/// to convert compatible types into Transport instances.

/// Create a client transport that spawns a child process
template <typename R>
TransportPtr<R> into_transport_stdio(asio::any_io_executor executor);

/// Create a transport from an existing process
template <typename R>
TransportPtr<R> into_transport_child(
    asio::any_io_executor executor,
    const std::string& program,
    const std::vector<std::string>& args = {});

} // namespace mcp
