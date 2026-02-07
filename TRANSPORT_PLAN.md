# Transport Layer Implementation Plan — C++ MCP SDK

## Status: COMPLETE

All deliverables have been implemented, built, tested, and verified.

---

## Context

The C++ MCP SDK (`/Users/vogler/Workspace/mcp/cpp-sdk`) has protocol-level parity with the Rust SDK but was missing several transport implementations. The Rust SDK provides transports for WebSocket, TCP helpers, Unix socket helpers, a Worker transport pattern, and HTTP upgrade — the C++ SDK only had Stdio, AsyncRw (template), ChildProcess (stubbed), and HTTP (client+server). Additionally, the ChildProcessTransport constructor was not implemented (just TODOs).

This plan implemented the missing transports to reach parity with the Rust SDK.

---

## Deliverables

### 1. Fix ChildProcessTransport -- COMPLETE

- Rewrote `include/mcp/transport/child_process_transport.hpp` — removed `boost::process` dependency, replaced with POSIX types (`pid_t`), guarded with `#if defined(__unix__) || defined(__APPLE__)`
- Rewrote `src/transport/child_process_transport.cpp` — full POSIX `fork()/pipe()/execvp()` implementation
- Unit tests: `ChildProcessTransport.SpawnAndCommunicate`, `SpawnWithArgs`, `CloseTerminatesChild`

### 2. WebSocketTransport -- COMPLETE

- Created `include/mcp/transport/websocket_transport.hpp` — `WebSocketTransport<R>` using `beast::websocket::stream<beast::tcp_stream>`, guarded with `#ifdef MCP_HTTP_TRANSPORT`
- Created `src/transport/websocket_transport.cpp` — send/receive/close using WebSocket message framing
- Helpers: `make_websocket_client_transport<R>()` and `accept_websocket_transport<R>()`

### 3. TCP and Unix Socket Helpers -- COMPLETE

- Expanded `include/mcp/transport/async_rw_transport.hpp`:
  - `TcpTransport<R>` type alias + `make_socket_transport<R>(tcp::socket)` factory
  - `UnixTransport<R>` type alias + `make_unix_socket_transport<R>(unix socket)` factory (POSIX only)
  - `make_pipe_transport<R>(executor, read_fd, write_fd)` factory
- Fixed `AsyncRwTransport::close()` — now always closes both reader and writer streams (was only closing reader when `ReadStream == WriteStream`)
- Unit tests: `PipeTransport.*` (4 tests), `TcpTransport.*` (3 tests), `UnixSocketTransport.*` (2 tests)

### 4. WorkerTransport -- COMPLETE

- Created `include/mcp/transport/worker_transport.hpp` — header-only
- `WorkerContext<R>` for bidirectional communication (`push_received()`, `next_outgoing()`)
- `WorkerTransport<R>::create(executor, worker_fn)` spawns user coroutine
- Fixed `WorkerContext` constructor accessibility for `std::make_shared`
- Unit tests: `WorkerTransport.BasicSendReceive`, `CloseStopsWorker`, `WorkerExceptionClosesTransport`, `MultipleMessages`

### 5. HTTP Upgrade Example -- COMPLETE

- Created `examples/http_upgrade_transport.cpp` — HTTP upgrade pattern using `make_socket_transport()` after 101 handshake

### 6. WebSocket Example -- COMPLETE

- Created `examples/websocket_transport.cpp` — WebSocket server+client using `accept_websocket_transport()` and `make_websocket_client_transport()`

### 7. CMake & Header Updates -- COMPLETE

- Updated `CMakeLists.txt` — added WebSocket source, removed `async_rw_transport.cpp` (now header-only), updated child process description
- Updated `examples/CMakeLists.txt` — added all 4 new example executables
- Updated `include/mcp/mcp.hpp` — added worker and websocket transport includes
- Implemented `include/mcp/transport/into_transport.hpp` — factory functions now have working implementations
- Removed unused `src/transport/async_rw_transport.cpp` placeholder file

---

## Additional cleanup performed

- Removed stale `build-debug/` directory (~206 MB)
- Added `.cache/` to `.gitignore`

---

## Test Summary

All 5 test suites pass (21 total tests):

| Suite | Tests | Status |
|-------|-------|--------|
| `test_model_serialization` | 14 | PASS |
| `test_jsonrpc` | 10 | PASS |
| `test_tool_router` | 5 | PASS |
| `test_service_loop` | 7 | PASS |
| `test_transports` | 16 | PASS |

**New transport tests (16 tests in `test_transports`):**
- PipeTransport: SendAndReceive, BidirectionalCommunication, CloseReturnsNullopt, MultipleMessages
- TcpTransport: SendAndReceive, Bidirectional, CloseReturnsNullopt
- UnixSocketTransport: SendAndReceive, Bidirectional
- WorkerTransport: BasicSendReceive, CloseStopsWorker, WorkerExceptionClosesTransport, MultipleMessages
- ChildProcessTransport: SpawnAndCommunicate, SpawnWithArgs, CloseTerminatesChild

---

## Verification

Build command:
```
cmake -B build -DMCP_BUILD_HTTP_TRANSPORT=ON -DMCP_BUILD_CHILD_PROCESS=ON -DMCP_BUILD_TESTS=ON -DMCP_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build
```

All examples run successfully end-to-end (TCP, Unix socket, WebSocket, HTTP upgrade).
