# C++ MCP SDK — Implementation Plan

## Gaps compared to Rust SDK (origin)

This document tracks what the C++ SDK still needs to reach full parity with the Rust SDK.

---

## 1. Transport Layer

### 1.1 WebSocket Transport Helper
- Rust has `SinkStreamTransport` that directly supports WebSocket via any Sink+Stream pair
- C++ needs a dedicated `WebSocketTransport` (or a generic `SinkStreamTransport` equivalent) built on Boost.Beast WebSocket
- Should support both client and server sides

### 1.2 Worker Transport
- Rust has a `WorkerTransport` that runs message processing in a background tokio task
- C++ equivalent: background `boost::asio` strand/executor-based transport wrapper
- Useful for offloading transport I/O to a dedicated thread/coroutine

### 1.3 TCP Socket Helper
- Rust provides a `make_tcp_transport` helper and example
- C++ `AsyncRwTransport` can already wrap TCP sockets, but needs a convenience factory function like `make_tcp_transport(host, port)` and a working example

### 1.4 Unix Socket Helper
- Same as TCP — needs a convenience factory `make_unix_socket_transport(path)` and example
- Only applicable on POSIX platforms

### 1.5 Named Pipe Transport (Windows)
- Rust has a named pipe example for Windows
- C++ needs a Windows named pipe transport helper (low priority, platform-specific)

### 1.6 HTTP Upgrade Transport
- Rust has an example upgrading HTTP connections to streaming
- C++ `StreamableHttpServer` may partially support this, but needs a dedicated upgrade path example

### 1.7 Streamable HTTP — Full Implementation
- Current C++ HTTP transport is marked as partial
- Ensure full feature parity: session management, reconnection, multi-stream support, proper SSE event handling

---

## 2. Handler Ergonomics

### 2.1 Router Composition
- Rust routers can be combined with the `+` operator (e.g., `router_a + router_b`)
- C++ `ToolRouter` and `PromptRouter` need an `operator+` or `merge()` method to combine multiple routers into one
- Enables modular server construction from independent feature modules

### 2.2 Auto JSON Schema Generation
- Rust uses `schemars` to derive JSON schemas from struct definitions automatically
- C++ has no equivalent — schemas must be written manually as JSON objects
- Options:
  - Build a compile-time reflection/macro system to generate schemas from C++ structs
  - Provide a `SchemaBuilder` utility beyond just elicitation (a general-purpose one for tool input/output schemas)
  - Consider integration with existing C++ reflection libraries

### 2.3 Task Handler Macro
- Rust has `#[task_handler]` proc macro that auto-generates task management using `OperationProcessor`
- C++ needs a `MCP_TASK_HANDLER` macro (or equivalent) that wires up task lifecycle methods (`list_tasks`, `get_task_info`, `get_task_result`, `cancel_task`) to a task manager automatically

### 2.4 Prompt Macros
- Rust has full `#[prompt]`, `#[prompt_router]`, `#[prompt_handler]` proc macros
- C++ has `PromptRouter` and `PromptRouterHandler` but lacks convenience macros like `MCP_PROMPT`, `MCP_PROMPT_ROUTE`, `MCP_PROMPT_ROUTER_INIT` (matching the tool macro pattern)
- Implement these for consistency

---

## 3. Examples

### 3.1 Counter Server (stdio + HTTP)
- Stateful counter with increment/get/decrement operations
- Demonstrate both stdio and HTTP streaming transports from the same handler

### 3.2 Prompt Server
- Server that exposes prompts with arguments
- Demonstrates `PromptRouter` usage and dynamic prompt content generation

### 3.3 Resource / Memory Server
- Dynamic resource management (create, read, update, delete resources at runtime)
- Demonstrates resource listing, reading, templates, subscriptions, and change notifications

### 3.4 Sampling Demo Server
- Server that requests LLM sampling from the client
- Demonstrates `create_message` flow from server side

### 3.5 Elicitation Demo Server
- Server that requests user input via elicitation
- Demonstrates `ElicitationSchemaBuilder` and response handling

### 3.6 Completion Demo Server
- Server that provides text completions for resource URIs or prompt arguments
- Demonstrates the `complete()` handler

### 3.7 Progress Demo
- Long-running tool operation with progress notifications
- Demonstrates `ProgressToken` usage and client-side progress tracking

### 3.8 Task Demo
- Server with long-running tasks
- Demonstrates task creation, polling, result retrieval, and cancellation

### 3.9 OAuth Authentication Examples
- Simple auth: basic client credentials flow
- Complex auth: full authorization code + PKCE flow with token refresh
- Demonstrate the existing `AuthorizationManager` in a complete working example

### 3.10 Transport Examples
- TCP socket client/server pair
- Unix socket client/server pair
- WebSocket client/server pair (once WebSocket transport is implemented)

### 3.11 Multi-Server Collection Client
- Client that connects to multiple MCP servers simultaneously
- Aggregates tools/resources/prompts across servers
- Demonstrates concurrent server management

### 3.12 Chat Client
- Standalone interactive chat application using MCP
- Demonstrates end-to-end integration with tool calling, sampling, and resource access

---

## 4. Logging Level Parity (informational — C++ already ahead)
- C++ has 8 logging levels (Debug, Info, Notice, Warning, Error, Critical, Alert, Emergency)
- Rust has 4 (Debug, Info, Warning, Error)
- No action needed — C++ is a superset

---

## Priority Order

### High Priority (core functionality gaps)
1. Streamable HTTP — full implementation (1.7)
2. WebSocket transport helper (1.1)
3. Router composition (2.1)
4. Prompt macros (2.4)

### Medium Priority (developer experience)
5. TCP/Unix socket helpers (1.3, 1.4)
6. Task handler macro (2.3)
7. Auto JSON schema generation (2.2)
8. Worker transport (1.2)

### Lower Priority (examples & documentation)
9. All examples (3.1–3.12)
10. HTTP upgrade transport (1.6)
11. Named pipe transport — Windows only (1.5)

---

## Notes
- The C++ SDK has **full protocol-level parity** with the Rust SDK for all MCP features
- All gaps are in transport helpers, developer ergonomics (macros/composition), and examples
- The OAuth module in C++ is already well-structured and arguably more complete than Rust
