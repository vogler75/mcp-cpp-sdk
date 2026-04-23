# Streamable HTTP Transport Parity Plan — C++ MCP SDK

## Status: PENDING

## Goal

Bring the C++ SDK's streamable HTTP transport (server and client) to parity with the Rust SDK. The current implementation is minimal: single-session, poll-based, no per-request routing, no SSE event caching, no client-side reconnect. This plan addresses all gaps.

---

## Gaps Addressed

| # | Gap | Priority | Phase |
|---|-----|----------|-------|
| 1 | SSE Event Caching & Resume (`Last-Event-Id`) | High | 2, 3 |
| 2 | Pluggable SessionManager Abstraction | High | 1, 2 |
| 3 | Per-Request SSE Stream Routing | High | 2, 3 |
| 4 | OneshotTransport (stateless mode) | Medium | 1 |
| 5 | SSE Auto-Reconnect (client-side) | Medium | 4 |
| 6 | NeverSessionManager (stateless mode) | Medium | 2 |
| 7 | Multi-session support | High | 2, 3 |

---

## Phase 1: Foundation Types

**Goal:** Introduce core abstractions that all subsequent phases depend on. Purely additive — no existing code is modified.

### New Files

#### `include/mcp/transport/session_id.hpp` (~30 lines)

```cpp
using SessionId = std::shared_ptr<std::string>;  // analogous to Rust's Arc<str>
SessionId make_session_id(std::string);
// Equality and hash support for use in maps
```

#### `include/mcp/transport/sse_message.hpp` (~60 lines)

```cpp
struct EventId {
    int64_t index;             // Sequential event counter
    int64_t http_request_id;   // Which HTTP request this event belongs to

    std::string to_string() const;  // Format: "<index>/<http_request_id>"
    static std::optional<EventId> parse(std::string_view);
};

struct ServerSseMessage {
    EventId event_id;
    std::optional<json> message;  // nullopt for priming/keepalive
    std::optional<std::chrono::milliseconds> retry_hint;

    std::string format() const;   // Format as SSE text block
};
```

#### `src/transport/sse_message.cpp` (~80 lines)

Implementation of `EventId::to_string()`, `EventId::parse()`, `ServerSseMessage::format()`.

#### `include/mcp/transport/session_manager.hpp` (~80 lines)

Abstract interface mirroring the Rust SDK's `SessionManager` trait:

```cpp
class SessionManager {
public:
    virtual ~SessionManager() = default;

    /// Create a new session, returning its ID.
    virtual asio::awaitable<SessionId> create_session() = 0;

    /// Called on initialize request. Returns a Transport for serve_server().
    virtual asio::awaitable<std::unique_ptr<Transport<RoleServer>>>
        initialize_session(const SessionId& id) = 0;

    /// Check whether a session exists.
    virtual bool has_session(const SessionId& id) const = 0;

    /// Close and remove a session.
    virtual asio::awaitable<void> close_session(const SessionId& id) = 0;

    /// Create an SSE stream for a specific HTTP request within a session.
    virtual asio::awaitable<SseStream>
        create_stream(const SessionId& id, int64_t http_request_id) = 0;

    /// Accept a JSON-RPC message into a session, associated with an HTTP request ID.
    virtual asio::awaitable<void> accept_message(
        const SessionId& id, int64_t http_request_id, json message) = 0;

    /// Create a standalone SSE stream (GET endpoint) for server-initiated messages.
    virtual asio::awaitable<SseStream>
        create_standalone_stream(const SessionId& id) = 0;

    /// Resume a stream from a given event ID (Last-Event-Id reconnection).
    virtual asio::awaitable<SseStream>
        resume(const SessionId& id, const EventId& last_event_id) = 0;
};
```

`SseStream` is an async channel yielding `ServerSseMessage` items using the codebase's existing `mutex + queue + steady_timer` pattern:

```cpp
class SseStream {
public:
    asio::awaitable<std::optional<ServerSseMessage>> next();
    void close();
};

class SseStreamSender {
public:
    void send(ServerSseMessage msg);
    void close();
};

std::pair<SseStreamSender, SseStream> make_sse_stream(asio::any_io_executor exec);
```

#### `include/mcp/transport/oneshot_transport.hpp` (~50 lines)

Stateless transport delivering exactly one message and collecting the response:

```cpp
class OneshotTransport : public Transport<RoleServer> {
public:
    explicit OneshotTransport(
        asio::any_io_executor executor,
        RxJsonRpcMessage<RoleServer> initial_message);

    asio::awaitable<void> send(TxJsonRpcMessage<RoleServer> msg) override;
    asio::awaitable<std::optional<RxJsonRpcMessage<RoleServer>>> receive() override;
    asio::awaitable<void> close() override;

    /// Get collected responses after transport is done.
    std::vector<TxJsonRpcMessage<RoleServer>> take_responses();
};
```

#### `src/transport/oneshot_transport.cpp` (~60 lines)

---

## Phase 2: LocalSessionManager (Core Session Logic)

**Goal:** Implement the per-session worker and message routing architecture. Still additive — no existing files are modified.

### New Files

#### `include/mcp/transport/local_session.hpp` (~100 lines)

Internal types for session management:

```cpp
using HttpRequestId = int64_t;

/// Key for routing server messages to the correct HTTP request's SSE stream.
using ResourceKey = std::variant<RequestId, ProgressToken>;

/// Wraps an SseStreamSender with a bounded cache for event replay.
class CachedSender {
public:
    explicit CachedSender(
        std::shared_ptr<SseStreamSender> sender,
        size_t max_cache_size = 64);

    void send(ServerSseMessage msg);
    std::vector<ServerSseMessage> replay_from(int64_t start_index) const;

private:
    std::shared_ptr<SseStreamSender> sender_;
    std::deque<ServerSseMessage> cache_;
    size_t max_cache_size_;
    mutable std::mutex mutex_;
};

/// Per-HTTP-request routing info within a session.
struct HttpRequestWise {
    HttpRequestId id;
    CachedSender sender;
    bool is_common = false;  // true for standalone GET streams
};
```

#### `include/mcp/transport/local_session_manager.hpp` (~80 lines)

```cpp
struct SessionConfig {
    size_t event_cache_size = 64;
    std::optional<std::chrono::milliseconds> sse_retry_hint;
};

class LocalSessionManager : public SessionManager {
public:
    explicit LocalSessionManager(
        asio::any_io_executor executor,
        SessionConfig config = {});

    // Full SessionManager interface implementation
    // ...

private:
    asio::any_io_executor executor_;
    SessionConfig config_;
    mutable std::shared_mutex sessions_mutex_;
    std::unordered_map<std::string, LocalSessionHandle> sessions_;
};
```

#### `src/transport/local_session_manager.cpp` (~500 lines)

Core implementation. Each session runs as a `WorkerTransport<RoleServer>` worker coroutine that maintains:

- `tx_router_`: `map<HttpRequestId, HttpRequestWise>` — maps HTTP request IDs to SSE senders
- `resource_router_`: `map<ResourceKey, HttpRequestId>` — maps JSON-RPC IDs and progress tokens to HTTP requests
- `next_event_index_`: monotonically increasing event counter
- `common_stream_id_`: the HttpRequestId of the standalone GET stream

**Routing logic in `resolve_outbound_channel()`:**
1. **Response/Error** (has `id`): Lookup JSON-RPC request ID in `resource_router_` -> find HTTP request -> send via that request's `CachedSender`. Remove routing entry.
2. **Progress notification** (has `progressToken`): Lookup progress token in `resource_router_` -> send via that request's `CachedSender`.
3. **Server-initiated requests/notifications**: Send via the "common" (standalone GET) stream.
4. **Fallback**: If no route found, send via common stream.

**`accept_message()` flow:**
1. Register JSON-RPC request ID in `resource_router_` -> `http_request_id`.
2. Extract any `progressToken` from `_meta` and register it too.
3. Forward message to `WorkerContext` (delivered to `serve_server()`'s receive loop).

#### `include/mcp/transport/never_session_manager.hpp` (~40 lines)
#### `src/transport/never_session_manager.cpp` (~80 lines)

Stateless mode: each POST gets an `OneshotTransport`. No session IDs, no SSE, no GET endpoint.

---

## Phase 3: Rewrite StreamableHttpServerTransport

**Goal:** Replace the current poll-based, single-session server with a SessionManager-based architecture.

### Modified Files

#### `include/mcp/transport/streamable_http_server.hpp` — Major rewrite

**Remove:**
- `struct ServerSession` entirely (replaced by SessionManager internals)
- `active_session_` member
- Inheritance from `Transport<RoleServer>` — the server becomes a service host, not a transport itself

**New API:**
```cpp
class StreamableHttpServerTransport {
public:
    explicit StreamableHttpServerTransport(
        asio::any_io_executor executor,
        StreamableHttpServerConfig config = {},
        std::shared_ptr<SessionManager> session_manager = nullptr);
        // nullptr -> creates LocalSessionManager or NeverSessionManager based on config

    /// Start the HTTP server. Spawns serve_server() per session internally.
    asio::awaitable<void> start(
        std::shared_ptr<ServerHandler> handler,
        CancellationToken cancellation = {});

    uint16_t port() const;
    asio::awaitable<void> stop();

private:
    std::shared_ptr<SessionManager> session_manager_;
    std::atomic<int64_t> next_http_request_id_{1};

    // Track running services per session
    std::mutex services_mutex_;
    std::unordered_map<std::string, RunningService<RoleServer>> services_;
};
```

**Config additions:**
```cpp
struct StreamableHttpServerConfig {
    // ... existing fields ...
    size_t event_cache_size = 64;
    bool json_response_mode = false;  // single-response POSTs return JSON instead of SSE
};
```

#### `src/transport/streamable_http_server.cpp` — Major rewrite

**`handle_post()` rewrite:**
```
1. Validate Content-Type and Accept headers
2. Parse JSON body
3. If no Mcp-Session-Id:
   a. Must be initialize request (stateful mode)
   b. session_manager_->create_session() -> SessionId
   c. session_manager_->initialize_session(sid) -> Transport
   d. Start serve_server() with that transport + handler
   e. http_request_id = next_http_request_id_++
   f. session_manager_->accept_message(sid, http_request_id, body)
   g. session_manager_->create_stream(sid, http_request_id)
   h. Stream SSE response from SseStream
   i. Set Mcp-Session-Id header
4. If Mcp-Session-Id present:
   a. Validate session via session_manager_->has_session()
   b. http_request_id = next_http_request_id_++
   c. session_manager_->accept_message(sid, http_request_id, body)
   d. If Accept includes text/event-stream:
      - create_stream(sid, http_request_id) -> stream SSE
   e. Otherwise: 202 Accepted
```

**`handle_get()` rewrite:**
```
1. Validate Accept and Mcp-Session-Id headers
2. Check for Last-Event-Id header
3. If Last-Event-Id: session_manager_->resume(sid, parsed_event_id)
4. Otherwise: session_manager_->create_standalone_stream(sid)
5. Write chunked SSE response headers
6. Loop: read from SseStream, write SSE chunks, keepalive on timeout
```

**`handle_delete()` rewrite:**
```
1. Validate Mcp-Session-Id header
2. session_manager_->close_session(sid)
3. Close corresponding RunningService
4. Return 200 or 202
```

### Breaking Change

`StreamableHttpServerTransport` no longer extends `Transport<RoleServer>`. Users must migrate from:
```cpp
// OLD
auto transport = std::make_shared<StreamableHttpServerTransport>(exec, config);
co_await serve_server(transport, handler, cancellation);
```
to:
```cpp
// NEW
StreamableHttpServerTransport server(exec, config);
co_await server.start(handler, cancellation);
```

---

## Phase 4: Client Transport Improvements

**Goal:** Add standalone GET SSE stream, auto-reconnect state machine, proper session lifecycle.

### Modified Files

#### `include/mcp/transport/streamable_http_client.hpp`

Add SSE auto-reconnect:

```cpp
enum class SseStreamState {
    Connected,
    Retrying,
    WaitingNextRetry,
    Terminated,
};

class SseAutoReconnectStream {
public:
    SseAutoReconnectStream(
        asio::any_io_executor executor,
        ParsedUrl url,
        std::optional<std::string> session_id,
        std::optional<std::string> auth_header,
        StreamableHttpClientConfig::SseRetryPolicy retry_policy);

    void start();
    asio::awaitable<std::optional<RxJsonRpcMessage<RoleClient>>> next();
    void close();

private:
    SseStreamState state_ = SseStreamState::Connected;
    int retry_attempt_ = 0;
    std::optional<std::string> last_event_id_;
};
```

Update `StreamableHttpClientTransport<R>`:
```cpp
// NEW members:
std::unique_ptr<SseAutoReconnectStream> sse_stream_;
bool open_sse_stream_ = true;

// NEW method:
void start_sse_stream();
```

#### `src/transport/streamable_http_client.cpp`

**After session establishment (first POST response with `Mcp-Session-Id`):**
Open standalone GET SSE connection via `start_sse_stream()`. Spawn background coroutine that reads from the stream and enqueues messages into `receive()`.

**SSE auto-reconnect state machine:**
```
Connected -> read SSE events from GET connection
    On disconnect:
        retry_policy.type == Never -> Terminated
        else -> Retrying (attempt = 0)

Retrying -> attempt reconnection with Last-Event-Id header
    On success -> Connected
    On failure:
        attempt >= max_retries -> Terminated
        else -> WaitingNextRetry

WaitingNextRetry -> wait retry_policy.delay_for(attempt)
    Then -> Retrying (attempt++)

Terminated -> return nullopt from next()
```

**GET SSE incremental reading** uses Beast's chunked response parsing:
```cpp
http::response_parser<http::string_body> parser;
parser.body_limit(boost::none);
co_await http::async_read_header(stream, buffer, parser, use_awaitable);
// Incrementally read and parse SSE events from chunked body
```

**`close()` changes:**
1. Close SSE stream if open.
2. Send DELETE to terminate session.
3. Wake up any waiting receivers.

---

## Phase 5: Integration, Testing, and Polish

**Goal:** End-to-end tests, example updates, build system changes.

### New Files

#### `tests/test_streamable_http.cpp` (~500 lines)

Test coverage:
1. **Session lifecycle**: Create -> Initialize -> Use -> Delete
2. **Per-request routing**: Two concurrent POSTs get responses routed correctly
3. **Standalone GET stream**: Server-initiated notifications arrive on GET stream
4. **Event caching/replay**: Reconnect with `Last-Event-Id` replays cached events
5. **Stateless mode**: NeverSessionManager works with OneshotTransport
6. **Client-server integration**: Full end-to-end with client + server transports
7. **Client auto-reconnect**: SSE stream reconnects after disconnect
8. **Multiple sessions**: Server handles multiple concurrent client sessions
9. **Progress routing**: Progress notifications route to the correct POST SSE stream
10. **DELETE cleanup**: Session resources are properly cleaned up

### Modified Files

#### `include/mcp/service/service.hpp`

Add convenience function:
```cpp
/// Start an HTTP server managing multiple client sessions.
asio::awaitable<void> serve_http(
    asio::any_io_executor executor,
    std::shared_ptr<ServerHandler> handler,
    StreamableHttpServerConfig config = {},
    CancellationToken cancellation = {});
```

#### `CMakeLists.txt`

Under the HTTP transport conditional, add:
```cmake
if(MCP_BUILD_HTTP_TRANSPORT)
    target_sources(mcp PRIVATE
        src/transport/sse_message.cpp
        src/transport/oneshot_transport.cpp
        src/transport/local_session_manager.cpp
        src/transport/never_session_manager.cpp
    )
endif()
```

#### `include/mcp/mcp.hpp`

Add new header includes (session_manager, local_session_manager, etc.).

### Example Updates

- Update `examples/echo_server.cpp` to use the new `server.start(handler)` API.
- Add `examples/http_multi_client.cpp` showing multiple concurrent clients.

---

## File Summary

### New Files (12)

| File | Phase | Approx Lines |
|------|-------|-------------|
| `include/mcp/transport/session_id.hpp` | 1 | ~30 |
| `include/mcp/transport/sse_message.hpp` | 1 | ~60 |
| `src/transport/sse_message.cpp` | 1 | ~80 |
| `include/mcp/transport/session_manager.hpp` | 1 | ~80 |
| `include/mcp/transport/oneshot_transport.hpp` | 1 | ~50 |
| `src/transport/oneshot_transport.cpp` | 1 | ~60 |
| `include/mcp/transport/local_session.hpp` | 2 | ~100 |
| `include/mcp/transport/local_session_manager.hpp` | 2 | ~80 |
| `src/transport/local_session_manager.cpp` | 2 | ~500 |
| `include/mcp/transport/never_session_manager.hpp` | 2 | ~40 |
| `src/transport/never_session_manager.cpp` | 2 | ~80 |
| `tests/test_streamable_http.cpp` | 5 | ~500 |

### Modified Files (6)

| File | Phase | Change |
|------|-------|--------|
| `include/mcp/transport/streamable_http_server.hpp` | 3 | Major rewrite: remove ServerSession, add SessionManager |
| `src/transport/streamable_http_server.cpp` | 3 | Major rewrite: new POST/GET/DELETE handlers |
| `include/mcp/transport/streamable_http_client.hpp` | 4 | Add SseAutoReconnectStream, update transport class |
| `src/transport/streamable_http_client.cpp` | 4 | Add GET SSE streaming, auto-reconnect, DELETE |
| `include/mcp/service/service.hpp` | 5 | Add `serve_http()` convenience |
| `CMakeLists.txt` | 5 | Add new source files |

### Removed Code

| What | File | Replaced By |
|------|------|-------------|
| `struct ServerSession` | `streamable_http_server.hpp` | SessionManager internals |
| Poll-based response waiting | `streamable_http_server.cpp` | SseStream async channel |
| `active_session_` member | `streamable_http_server.hpp` | SessionManager |

---

## Key Design Decisions

### 1. SessionManager as Dependency Injection
`StreamableHttpServerTransport` owns a `shared_ptr<SessionManager>`. Users can inject custom implementations or let the server create `LocalSessionManager` (stateful) or `NeverSessionManager` (stateless) based on config.

### 2. Reuse Existing Channel Pattern
All async channels (`SseStream`, session control) use the codebase's `mutex + queue + steady_timer` pattern. No new concurrency primitives.

### 3. Session Worker = WorkerTransport
Each session runs as a `WorkerTransport<RoleServer>` coroutine. This keeps `serve_server()` unchanged — it just sees a normal `Transport<RoleServer>`.

### 4. Event ID Format
`<index>/<http_request_id>` format (matching Rust). Enables monotonic ordering and identifies which HTTP request stream an event belongs to for resumption routing.

### 5. Breaking API Change
`StreamableHttpServerTransport` no longer extends `Transport<RoleServer>`. It becomes a service host that manages transports internally. This is necessary for multi-session support.

### 6. Stateless Mode
`NeverSessionManager` + `OneshotTransport`: each POST creates a fresh transport, processes one request-response cycle, returns the response inline. No session IDs, no SSE, no GET endpoint.

---

## Estimated Effort

| Phase | Description | New Code | Modified Code | Complexity |
|-------|-------------|----------|---------------|------------|
| 1 | Foundation types | ~360 lines | 0 | Low |
| 2 | LocalSessionManager | ~800 lines | 0 | High |
| 3 | Server rewrite | ~200 new, ~500 rewritten | ~770 lines | High |
| 4 | Client improvements | ~300 new, ~200 rewritten | ~520 lines | Medium |
| 5 | Tests, examples, polish | ~600 lines | ~20 lines | Medium |
| **Total** | | **~2,260 new** | **~1,300 modified** | |

---

## Verification

Each phase should build and pass tests before proceeding:

```bash
cmake -B build -DMCP_BUILD_HTTP_TRANSPORT=ON -DMCP_BUILD_CHILD_PROCESS=ON \
    -DMCP_BUILD_TESTS=ON -DMCP_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build
```
