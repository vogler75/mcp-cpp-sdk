# MCP C++ SDK

An in-progress C++20 implementation of the Model Context Protocol (MCP), derived from the architecture and behavior of the sibling [`../rust-sdk`](../rust-sdk). The SDK is built around `Boost.Asio` coroutines, `nlohmann/json`, and a transport/service split that closely mirrors the Rust implementation.

The repository currently provides:

- MCP model types and JSON-RPC message serialization
- Client and server service loops with initialization handshake handling
- Base handler interfaces for MCP server and client roles
- Tool and prompt routers for quickly exposing capabilities
- Multiple transports: stdio, generic async read/write, TCP, Unix domain sockets, optional streamable HTTP, and WebSocket support
- OAuth 2.1 helper types for streamable HTTP clients

## Status

This codebase is the C++ counterpart to the Rust SDK and follows the same overall design, but it is not yet packaged the same way as the Rust project. Today the project is best consumed as a CMake subdirectory or via `FetchContent`.

Protocol version support is modeled in [`include/mcp/model/types.hpp`](include/mcp/model/types.hpp), with `LATEST` currently set to `2025-11-25`.

## Repository Layout

- [`include/mcp`](include/mcp): public headers
- [`src`](src): protocol, service, handler, transport, auth, and task-manager implementation
- [`examples`](examples): runnable example servers, clients, and transport demos
- [`tests`](tests): unit and integration-style tests

The umbrella header [`include/mcp/mcp.hpp`](include/mcp/mcp.hpp) pulls in the full public surface.

## Build

### Requirements

- CMake 3.20+
- A C++20 compiler
- Boost 1.82+

The project fetches these dependencies automatically:

- `nlohmann/json`
- `spdlog`
- `googletest` when tests are enabled

### Configure and Build

```bash
cmake -S . -B build
cmake --build build
```

### Run Tests

```bash
ctest --test-dir build --output-on-failure
```

### CMake Options

```text
MCP_BUILD_TESTS=ON
MCP_BUILD_EXAMPLES=ON
MCP_BUILD_HTTP_TRANSPORT=ON
MCP_BUILD_CHILD_PROCESS=OFF
```

Notes:

- `MCP_BUILD_HTTP_TRANSPORT` enables the streamable HTTP and WebSocket codepaths.
- `MCP_BUILD_CHILD_PROCESS` enables the POSIX fork/pipe transport and is off by default.

## Using the SDK

The current integration model is straightforward:

```cmake
add_subdirectory(path/to/cpp-sdk)
target_link_libraries(your_target PRIVATE mcp::mcp)
```

Then include either the umbrella header:

```cpp
#include "mcp/mcp.hpp"
```

or only the specific headers you need.

## Quick Start: Stdio Tool Server

This is the smallest server shape supported by the current API: subclass `ToolRouterHandler`, register tools, then call `serve_server(...)`.

```cpp
#include <boost/asio.hpp>
#include "mcp/mcp.hpp"

namespace asio = boost::asio;

class CalculatorHandler : public mcp::ToolRouterHandler {
public:
    CalculatorHandler() {
        router_.add_tool(
            "add",
            "Add two numbers",
            mcp::json{
                {"type", "object"},
                {"properties", {
                    {"a", {{"type", "number"}}},
                    {"b", {{"type", "number"}}}
                }},
                {"required", {"a", "b"}}
            },
            [](mcp::ToolCallContext ctx) -> asio::awaitable<mcp::CallToolResult> {
                auto args = ctx.arguments.value_or(mcp::JsonObject{});
                double a = args.count("a") ? args.at("a").get<double>() : 0.0;
                double b = args.count("b") ? args.at("b").get<double>() : 0.0;
                co_return mcp::CallToolResult::success({
                    mcp::Content::text(std::to_string(a + b))
                });
            });
    }
};

int main() {
    asio::io_context io;
    auto handler = std::make_shared<CalculatorHandler>();
    auto transport =
        std::make_unique<mcp::StdioTransport<mcp::RoleServer>>(io.get_executor());

    asio::co_spawn(
        io,
        [&]() -> asio::awaitable<void> {
            auto service = co_await mcp::serve_server(std::move(transport), handler);
            co_await service.wait();
        },
        asio::detached);

    io.run();
}
```

For a complete working version, see [`examples/calculator_server.cpp`](examples/calculator_server.cpp).

## Client and Server APIs

The two service entry points are:

- `mcp::serve_server(...)` in [`include/mcp/service/service.hpp`](include/mcp/service/service.hpp)
- `mcp::serve_client(...)` in [`include/mcp/service/service.hpp`](include/mcp/service/service.hpp)

They return `RunningService<RoleServer>` or `RunningService<RoleClient>`, which provide:

- `peer()` to send requests and notifications
- `peer_info()` to inspect negotiated peer metadata
- `close()` / `cancel()` for shutdown
- `wait()` to await service completion

The handler base classes mirror the Rust SDK split:

- [`ServerHandler`](include/mcp/handler/server_handler.hpp) for tools, prompts, resources, completion, logging, tasks, and notifications
- [`ClientHandler`](include/mcp/handler/client_handler.hpp) for sampling, roots, elicitation, and client-side notifications

## Convenience Layers

### Tool Router

[`ToolRouter`](include/mcp/handler/tool_router.hpp) and `ToolRouterHandler` are the easiest way to expose MCP tools from a server.

### Prompt Router

[`PromptRouter`](include/mcp/handler/prompt_router.hpp) and `PromptRouterHandler` provide the same pattern for prompts.

### Macros

The SDK includes C preprocessor helpers that mirror the Rust macro ergonomics:

- [`MCP_TOOL`, `MCP_TOOL_ROUTE`, `MCP_TOOL_HANDLER`](include/mcp/macros/tool_macros.hpp)
- [`MCP_PROMPT`, `MCP_PROMPT_ROUTE`, `MCP_PROMPT_HANDLER`](include/mcp/macros/prompt_macros.hpp)

These are convenience helpers, not a separate codegen step.

## Transports

### Always Available

- `StdioTransport<R>` for newline-delimited JSON over stdin/stdout
- `AsyncRwTransport<R, ReadStream, WriteStream>` for generic Asio stream pairs
- `make_pipe_transport(...)` for pipe-backed transports
- `make_socket_transport(...)` for TCP sockets
- `make_unix_socket_transport(...)` for Unix domain sockets on supported platforms

### Optional HTTP Transport

When `MCP_BUILD_HTTP_TRANSPORT=ON`, the SDK also builds:

- `StreamableHttpClientTransport<R>`
- `StreamableHttpServerTransport`
- `WebSocket` transport support
- Session managers for stateful and stateless HTTP sessions

See:

- [`examples/http_test_server.cpp`](examples/http_test_server.cpp)
- [`examples/websocket_transport.cpp`](examples/websocket_transport.cpp)
- [`examples/http_upgrade_transport.cpp`](examples/http_upgrade_transport.cpp)

## Authentication

[`include/mcp/auth/auth.hpp`](include/mcp/auth/auth.hpp) contains OAuth 2.1 support for HTTP-based clients, including:

- authorization-server metadata discovery
- PKCE verifier/challenge generation
- dynamic client registration support
- token exchange and refresh helpers
- in-memory credential and state stores

## Examples

Current example programs include:

- [`examples/calculator_server.cpp`](examples/calculator_server.cpp): stdio tool server
- [`examples/echo_server.cpp`](examples/echo_server.cpp): basic echo server
- [`examples/simple_client.cpp`](examples/simple_client.cpp): in-process client/server handshake and tool calling
- [`examples/tcp_transport.cpp`](examples/tcp_transport.cpp): TCP transport
- [`examples/unix_socket_transport.cpp`](examples/unix_socket_transport.cpp): Unix socket transport
- [`examples/websocket_transport.cpp`](examples/websocket_transport.cpp): WebSocket transport
- [`examples/http_test_server.cpp`](examples/http_test_server.cpp): streamable HTTP server with tools, prompts, resources, and bearer auth

## Relationship to the Rust SDK

This project intentionally tracks the structure of the sibling Rust implementation in [`../rust-sdk`](../rust-sdk):

- protocol model types are organized by MCP domain
- service startup is split into client and server initialization
- handler traits/interfaces are role-specific
- routers provide the ergonomic path for tool and prompt registration
- transport-specific code stays separate from the protocol model and dispatch logic

If you are looking for the most feature-complete reference implementation, use the Rust SDK as the behavioral source of truth and the C++ SDK as the corresponding port.
