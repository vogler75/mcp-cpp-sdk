# Transport Layer Implementation Plan — C++ MCP SDK

## Context

The C++ MCP SDK (`/Users/vogler/Workspace/mcp/cpp-sdk`) has protocol-level parity with the Rust SDK but is missing several transport implementations. The Rust SDK provides transports for WebSocket, TCP helpers, Unix socket helpers, a Worker transport pattern, and HTTP upgrade — the C++ SDK only has Stdio, AsyncRw (template), ChildProcess (stubbed), and HTTP (client+server). Additionally, the ChildProcessTransport constructor is not implemented (just TODOs).

This plan implements the missing transports to reach parity with the Rust SDK.

---

## Deliverables (in order)

### 1. Fix ChildProcessTransport — complete the stubbed implementation

**Files to modify:**
- `src/transport/child_process_transport.cpp` — implement constructor, destructor, pid()
- `include/mcp/transport/child_process_transport.hpp` — minor adjustments if needed

**What to do:**
- Implement constructor using POSIX `fork`/`pipe`/`execvp`
- Create stdin/stdout pipes using `::pipe()`
- Fork and exec the child process with program, args, and env
- Wrap pipe FDs in `asio::posix::stream_descriptor`
- Implement destructor: send SIGTERM, waitpid with timeout, then SIGKILL
- Implement `pid()` to return the actual child PID
- Keep the existing send/receive/close logic (already correct)

**Approach:** Use raw POSIX `fork/pipe/execvp` rather than boost::process::v2, since:
- Boost.Process v2 has inconsistent API across Boost versions
- POSIX fork/pipe is simpler, well-understood, and sufficient for MCP's needs
- Guard with `#if defined(__unix__) || defined(__APPLE__)` for POSIX systems

---

### 2. WebSocketTransport — new transport using Boost.Beast WebSocket

**New files:**
- `include/mcp/transport/websocket_transport.hpp`
- `src/transport/websocket_transport.cpp`

**What to do:**
- Create `WebSocketTransport<R>` extending `Transport<R>`
- Wraps `beast::websocket::stream<beast::tcp_stream>`
- Unlike AsyncRwTransport (line-delimited JSON), WebSocket uses message framing — each WebSocket text message = one JSON-RPC message
- `send()`: serialize to JSON, send as WebSocket text message via `ws.async_write()`
- `receive()`: `ws.async_read()` into buffer, parse JSON, return message. Return nullopt on close.
- `close()`: `ws.async_close(websocket::close_code::normal)`
- Guard with `#ifdef MCP_HTTP_TRANSPORT` (reuses Boost.Beast, same dependency)
- Add client helper: `make_websocket_client_transport(executor, host, port, path)` — performs TCP connect + WebSocket handshake
- Add server helper: `accept_websocket_transport(tcp::socket)` — performs WebSocket accept handshake

**Design follows Rust pattern:** The Rust SDK wraps `tokio-tungstenite` WebSocket as a `Sink+Stream` and uses `SinkStreamTransport`. In C++, we directly wrap `beast::websocket::stream` which is simpler.

---

### 3. TCP and Unix Socket Helpers — convenience factories + examples

**Files to modify:**
- `include/mcp/transport/async_rw_transport.hpp` — add `make_unix_socket_transport()` factory
- Ensure `make_socket_transport()` implementation works (currently template-only, needs to be verified/completed)

**New example files:**
- `examples/tcp_transport.cpp` — TCP client+server pair using AsyncRwTransport
- `examples/unix_socket_transport.cpp` — Unix socket client+server pair

**TCP example pattern (mirrors Rust):**
```
Server: bind TCP listener → accept connections → wrap each socket via make_socket_transport<RoleServer>() → serve_server()
Client: connect TCP socket → wrap via make_socket_transport<RoleClient>() → serve_client()
```

**Unix socket pattern:**
```
Server: bind local::stream_protocol::acceptor → accept → wrap via make_unix_socket_transport<RoleServer>() → serve_server()
Client: connect local::stream_protocol::socket → wrap via make_unix_socket_transport<RoleClient>() → serve_client()
```

---

### 4. WorkerTransport — background coroutine transport pattern

**New files:**
- `include/mcp/transport/worker_transport.hpp`

**What to do:**
- Template class `WorkerTransport<R>` extending `Transport<R>`
- Runs a user-provided coroutine in a background `co_spawn`
- Communicates via two async queues (using the existing timer-signaling pattern from HTTP transport):
  - `to_handler_queue_`: messages from worker → handler (for receive())
  - `from_handler_queue_`: messages from handler → worker (for send())
- Worker coroutine receives a `WorkerContext` with:
  - Method to push received messages to handler
  - Method to get messages to send from handler
  - CancellationToken
- `send()`: push message to from_handler_queue_, signal worker
- `receive()`: pop from to_handler_queue_, wait on signal if empty
- `close()`: cancel worker, join background task
- Factory: `WorkerTransport<R>::spawn(worker_fn, executor, cancellation)`

**Use case:** Complex transports that need independent bidirectional processing (e.g., SSE reconnection, custom protocol adapters).

---

### 5. HTTP Upgrade Transport — example showing upgrade pattern

**New example file:**
- `examples/http_upgrade_transport.cpp`

**What to do:**
- Server: Accept HTTP connection with Boost.Beast, check for `Upgrade: mcp` header, respond with 101 Switching Protocols, then use the raw TCP socket with `make_socket_transport()`
- Client: Connect TCP, send HTTP upgrade request, receive 101 response, then use raw socket with `make_socket_transport()`
- This is an example/pattern, not a new transport class — reuses AsyncRwTransport after the upgrade handshake

---

### 6. WebSocket Transport Example

**New example file:**
- `examples/websocket_transport.cpp`

**What to do:**
- Server: TCP listener → accept → WebSocket handshake → `WebSocketTransport<RoleServer>` → `serve_server()`
- Client: TCP connect → WebSocket handshake → `WebSocketTransport<RoleClient>` → `serve_client()`
- Use the calculator handler from existing examples to demonstrate functionality

---

### 7. CMake & Umbrella Header Updates

**Files to modify:**
- `CMakeLists.txt`:
  - Add `src/transport/websocket_transport.cpp` to `MCP_ALL_SOURCES` (under `MCP_BUILD_HTTP_TRANSPORT` guard)
  - Add new examples to `examples/CMakeLists.txt`
  - Change `MCP_BUILD_CHILD_PROCESS` implementation to POSIX (remove Boost.Process dependency)
- `include/mcp/mcp.hpp`:
  - Add `#include "mcp/transport/websocket_transport.hpp"` under `MCP_HTTP_TRANSPORT` guard
  - Add `#include "mcp/transport/worker_transport.hpp"`
- `examples/CMakeLists.txt`:
  - Add tcp_transport, unix_socket_transport, websocket_transport, http_upgrade_transport executables

---

## File Summary

| Action | File |
|--------|------|
| **Modify** | `src/transport/child_process_transport.cpp` |
| **Modify** | `include/mcp/transport/child_process_transport.hpp` |
| **Create** | `include/mcp/transport/websocket_transport.hpp` |
| **Create** | `src/transport/websocket_transport.cpp` |
| **Modify** | `include/mcp/transport/async_rw_transport.hpp` |
| **Create** | `include/mcp/transport/worker_transport.hpp` |
| **Create** | `examples/tcp_transport.cpp` |
| **Create** | `examples/unix_socket_transport.cpp` |
| **Create** | `examples/websocket_transport.cpp` |
| **Create** | `examples/http_upgrade_transport.cpp` |
| **Modify** | `CMakeLists.txt` |
| **Modify** | `examples/CMakeLists.txt` |
| **Modify** | `include/mcp/mcp.hpp` |

---

## Priority Order

1. **ChildProcessTransport fix** (unblocks child process usage)
2. **WebSocketTransport** (most requested transport gap)
3. **TCP/Unix socket helpers + examples** (easy wins, demonstrate AsyncRwTransport)
4. **WorkerTransport** (enables complex transport patterns)
5. **HTTP Upgrade example** (demonstrates upgrade pattern)
6. **WebSocket example** (demonstrates WebSocketTransport end-to-end)
7. **CMake & header updates** (wires everything together)

---

## Verification

1. **Build:** `cmake --build build` — all new files compile without errors
2. **Existing tests pass:** `cd build && ctest` — no regressions
3. **TCP example:** Run tcp_transport example — server and client communicate, list tools, call tool
4. **Unix socket example:** Run unix_socket_transport — same verification
5. **WebSocket example:** Run websocket_transport — WebSocket handshake succeeds, tools work
6. **HTTP upgrade example:** Run http_upgrade_transport — upgrade handshake, then MCP communication works
7. **Child process:** Enable `MCP_BUILD_CHILD_PROCESS`, build, verify it can spawn and communicate with a child MCP server
