#ifdef MCP_HTTP_TRANSPORT

#include "mcp/transport/local_session_manager.hpp"

#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

#include <spdlog/spdlog.h>

namespace mcp {

// =============================================================================
// UUID v4 generation (reuses same approach as streamable_http_server.cpp)
// =============================================================================

namespace {

std::string generate_uuid_v4() {
    static thread_local std::mt19937 rng(
        static_cast<unsigned>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()));

    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    auto rand32 = [&]() { return dist(rng); };

    uint32_t a = rand32();
    uint16_t b = static_cast<uint16_t>(rand32() & 0xFFFF);
    uint16_t c = static_cast<uint16_t>((rand32() & 0x0FFF) | 0x4000);
    uint16_t d = static_cast<uint16_t>((rand32() & 0x3FFF) | 0x8000);
    uint32_t e1 = rand32();
    uint16_t e2 = static_cast<uint16_t>(rand32() & 0xFFFF);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << a << '-'
        << std::setw(4) << b << '-'
        << std::setw(4) << c << '-'
        << std::setw(4) << d << '-'
        << std::setw(8) << e1
        << std::setw(4) << e2;
    return oss.str();
}

} // anonymous namespace

// =============================================================================
// Helper: extract JSON-RPC metadata from raw JSON for routing registration
// =============================================================================

namespace {

/// Extract the JSON-RPC request ID from a raw JSON message.
/// Returns nullopt if the message has no "id" field (i.e., it's a notification).
std::optional<RequestId> extract_request_id(const json& msg) {
    if (!msg.contains("id")) return std::nullopt;
    const auto& id_val = msg["id"];
    if (id_val.is_number_integer()) {
        return RequestId(id_val.get<int64_t>());
    }
    if (id_val.is_string()) {
        return RequestId(id_val.get<std::string>());
    }
    return std::nullopt;
}

/// Extract the progress token from a raw JSON message's params._meta.progressToken.
std::optional<ProgressToken> extract_progress_token(const json& msg) {
    if (!msg.contains("params")) return std::nullopt;
    const auto& params = msg["params"];
    if (!params.is_object() || !params.contains("_meta")) return std::nullopt;
    const auto& meta = params["_meta"];
    if (!meta.is_object() || !meta.contains("progressToken")) return std::nullopt;

    const auto& pt = meta["progressToken"];
    if (pt.is_number_integer()) {
        return ProgressToken(NumberOrString(pt.get<int64_t>()));
    }
    if (pt.is_string()) {
        return ProgressToken(NumberOrString(pt.get<std::string>()));
    }
    return std::nullopt;
}

/// Extract the progress token from an outgoing server notification.
/// For TxJsonRpcMessage<RoleServer> notifications, we need to serialize to JSON
/// to extract the progressToken. This is used in route_outgoing() for progress
/// notifications.
std::optional<ProgressToken> extract_notification_progress_token(
    const TxJsonRpcMessage<RoleServer>& msg) {
    if (!msg.is_notification()) return std::nullopt;
    // Check if it's a ProgressNotification
    const auto& noti = msg.as_notification().notification;
    if (!noti.template is<ProgressNotification>()) return std::nullopt;
    return noti.template get<ProgressNotification>().params.progress_token;
}

/// Extract the JSON-RPC request ID from an outgoing response or error message.
std::optional<RequestId> extract_outgoing_id(const TxJsonRpcMessage<RoleServer>& msg) {
    if (msg.is_response()) {
        return msg.as_response().id;
    }
    if (msg.is_error()) {
        return msg.as_error().id;
    }
    return std::nullopt;
}

} // anonymous namespace

// =============================================================================
// LocalSessionManager constructor
// =============================================================================

LocalSessionManager::LocalSessionManager(
    asio::any_io_executor executor,
    SessionConfig config)
    : executor_(std::move(executor))
    , config_(std::move(config)) {}

// =============================================================================
// get_handle()
// =============================================================================

std::shared_ptr<LocalSessionHandle>
LocalSessionManager::get_handle(const SessionId& id) const {
    if (!id) return nullptr;
    std::shared_lock<std::shared_mutex> lock(sessions_mutex_);
    auto it = sessions_.find(*id);
    if (it == sessions_.end()) return nullptr;
    return it->second;
}

// =============================================================================
// create_session()
// =============================================================================

asio::awaitable<SessionId> LocalSessionManager::create_session() {
    auto session_id_str = generate_uuid_v4();
    auto sid = make_session_id(session_id_str);

    auto handle = std::make_shared<LocalSessionHandle>();
    handle->id = sid;

    {
        std::unique_lock<std::shared_mutex> lock(sessions_mutex_);
        sessions_[*sid] = handle;
    }

    spdlog::debug("LocalSessionManager: created session {}", *sid);
    co_return sid;
}

// =============================================================================
// initialize_session()
// =============================================================================

asio::awaitable<std::unique_ptr<Transport<RoleServer>>>
LocalSessionManager::initialize_session(const SessionId& id) {
    auto handle = get_handle(id);
    if (!handle) {
        throw std::runtime_error("LocalSessionManager: session not found for initialize");
    }

    // Create a WorkerTransport whose worker coroutine reads outgoing messages
    // and routes them to the correct SSE stream.
    auto transport = WorkerTransport<RoleServer>::create(
        executor_,
        [this, handle](std::shared_ptr<WorkerContext<RoleServer>> ctx)
            -> asio::awaitable<void> {
            // Store the worker context in the session handle
            {
                std::lock_guard<std::mutex> lock(handle->mutex);
                handle->worker_ctx = ctx;
            }

            // Worker loop: read outgoing messages from the handler and route them
            while (true) {
                auto msg_opt = co_await ctx->next_outgoing();
                if (!msg_opt) break; // Worker closed

                route_outgoing(handle, std::move(*msg_opt));
            }

            spdlog::debug("LocalSessionManager: worker coroutine ended for session {}",
                *handle->id);
        });

    spdlog::debug("LocalSessionManager: initialized session {}", *id);
    co_return std::move(transport);
}

// =============================================================================
// has_session()
// =============================================================================

bool LocalSessionManager::has_session(const SessionId& id) const {
    if (!id) return false;
    std::shared_lock<std::shared_mutex> lock(sessions_mutex_);
    return sessions_.find(*id) != sessions_.end();
}

// =============================================================================
// close_session()
// =============================================================================

asio::awaitable<void> LocalSessionManager::close_session(const SessionId& id) {
    std::shared_ptr<LocalSessionHandle> handle;

    {
        std::unique_lock<std::shared_mutex> lock(sessions_mutex_);
        auto it = sessions_.find(*id);
        if (it == sessions_.end()) co_return;
        handle = it->second;
        sessions_.erase(it);
    }

    spdlog::debug("LocalSessionManager: closing session {}", *id);

    std::lock_guard<std::mutex> lock(handle->mutex);
    handle->closed = true;

    // Note: We don't close the WorkerContext directly (it's managed by the
    // WorkerTransport which is owned by the serve_server caller).
    // Clearing the worker_ctx ref lets the worker coroutine finish naturally
    // when the transport is closed by its owner.
    handle->worker_ctx.reset();

    // Close all SSE stream senders
    for (auto& [req_id, request_wise] : handle->tx_router) {
        if (request_wise.sender) {
            request_wise.sender->close();
        }
    }
    handle->tx_router.clear();
    handle->resource_router.clear();

    co_return;
}

// =============================================================================
// create_stream()
// =============================================================================

asio::awaitable<SseStream>
LocalSessionManager::create_stream(const SessionId& id, int64_t http_request_id) {
    auto handle = get_handle(id);
    if (!handle) {
        throw std::runtime_error("LocalSessionManager: session not found for create_stream");
    }

    auto [sender, stream] = make_sse_stream(executor_);
    auto shared_sender = std::make_shared<SseStreamSender>(std::move(sender));

    // Send priming event if retry hint is configured
    if (config_.sse_retry_hint) {
        auto event_index = handle->next_event_index.fetch_add(1);
        auto priming = ServerSseMessage::priming(
            EventId{event_index, http_request_id},
            *config_.sse_retry_hint);
        shared_sender->send(std::move(priming));
    }

    // Create a CachedSender and register it in the tx_router
    auto cached_sender = std::make_shared<CachedSender>(
        shared_sender, config_.event_cache_size);

    {
        std::lock_guard<std::mutex> lock(handle->mutex);
        HttpRequestWise req_wise;
        req_wise.id = http_request_id;
        req_wise.sender = cached_sender;
        req_wise.is_common = false;
        handle->tx_router[http_request_id] = std::move(req_wise);
    }

    spdlog::debug("LocalSessionManager: created stream for session {} http_request_id={}",
        *id, http_request_id);

    co_return std::move(stream);
}

// =============================================================================
// accept_message()
// =============================================================================

asio::awaitable<void> LocalSessionManager::accept_message(
    const SessionId& id, int64_t http_request_id, json message) {
    auto handle = get_handle(id);
    if (!handle) {
        throw std::runtime_error("LocalSessionManager: session not found for accept_message");
    }

    // Extract JSON-RPC request ID and progress token for routing
    auto req_id = extract_request_id(message);
    auto progress_token = extract_progress_token(message);

    // Register routing entries
    {
        std::lock_guard<std::mutex> lock(handle->mutex);
        if (req_id) {
            handle->resource_router[ResourceKey{*req_id}] = http_request_id;
            spdlog::trace("LocalSessionManager: registered request id route: {} -> http_req {}",
                req_id->to_string(), http_request_id);
        }
        if (progress_token) {
            handle->resource_router[ResourceKey{progress_token->value}] = http_request_id;
            spdlog::trace("LocalSessionManager: registered progress token route -> http_req {}",
                http_request_id);
        }
    }

    // Parse the JSON into an RxJsonRpcMessage and push to the worker context
    try {
        RxJsonRpcMessage<RoleServer> rx_msg = message.get<RxJsonRpcMessage<RoleServer>>();

        std::shared_ptr<WorkerContext<RoleServer>> ctx;
        {
            std::lock_guard<std::mutex> lock(handle->mutex);
            ctx = handle->worker_ctx;
        }

        if (ctx) {
            ctx->push_received(std::move(rx_msg));
        } else {
            spdlog::warn("LocalSessionManager: worker context not ready for session {}", *id);
        }
    } catch (const std::exception& e) {
        spdlog::error("LocalSessionManager: failed to parse JSON-RPC message: {}", e.what());
        throw;
    }

    co_return;
}

// =============================================================================
// create_standalone_stream()
// =============================================================================

asio::awaitable<SseStream>
LocalSessionManager::create_standalone_stream(const SessionId& id) {
    auto handle = get_handle(id);
    if (!handle) {
        throw std::runtime_error(
            "LocalSessionManager: session not found for create_standalone_stream");
    }

    auto [sender, stream] = make_sse_stream(executor_);
    auto shared_sender = std::make_shared<SseStreamSender>(std::move(sender));

    // Send priming event if retry hint is configured
    // For standalone streams, use 0 as the http_request_id since it's a
    // common stream not tied to a specific POST request
    int64_t common_id;
    {
        std::lock_guard<std::mutex> lock(handle->mutex);
        // Use a negative ID space for common streams to avoid collision
        // with POST request IDs (which are positive)
        common_id = -(handle->next_event_index.fetch_add(1));
    }

    if (config_.sse_retry_hint) {
        auto event_index = handle->next_event_index.fetch_add(1);
        auto priming = ServerSseMessage::priming(
            EventId{event_index, common_id},
            *config_.sse_retry_hint);
        shared_sender->send(std::move(priming));
    }

    auto cached_sender = std::make_shared<CachedSender>(
        shared_sender, config_.event_cache_size);

    {
        std::lock_guard<std::mutex> lock(handle->mutex);

        // Close previous common stream if any
        if (handle->common_stream_id) {
            auto it = handle->tx_router.find(*handle->common_stream_id);
            if (it != handle->tx_router.end()) {
                if (it->second.sender) {
                    it->second.sender->close();
                }
                handle->tx_router.erase(it);
            }
        }

        HttpRequestWise req_wise;
        req_wise.id = common_id;
        req_wise.sender = cached_sender;
        req_wise.is_common = true;
        handle->tx_router[common_id] = std::move(req_wise);
        handle->common_stream_id = common_id;
    }

    spdlog::debug("LocalSessionManager: created standalone stream for session {} common_id={}",
        *id, common_id);

    co_return std::move(stream);
}

// =============================================================================
// resume()
// =============================================================================

asio::awaitable<SseStream>
LocalSessionManager::resume(const SessionId& id, const EventId& last_event_id) {
    auto handle = get_handle(id);
    if (!handle) {
        throw std::runtime_error("LocalSessionManager: session not found for resume");
    }

    auto [sender, stream] = make_sse_stream(executor_);
    auto shared_sender = std::make_shared<SseStreamSender>(std::move(sender));

    {
        std::lock_guard<std::mutex> lock(handle->mutex);

        // Find the stream to resume based on http_request_id in the EventId
        auto target_id = last_event_id.http_request_id;
        auto it = handle->tx_router.find(target_id);

        if (it != handle->tx_router.end() && it->second.sender) {
            // Replay cached events from the old sender
            it->second.sender->replay_from(last_event_id.index, *shared_sender);

            // Replace the underlying sender in the CachedSender so future
            // events go to the new stream
            it->second.sender->replace_sender(shared_sender);
        } else {
            // The original stream is gone. Create a new entry.
            auto cached_sender = std::make_shared<CachedSender>(
                shared_sender, config_.event_cache_size);

            HttpRequestWise req_wise;
            req_wise.id = target_id;
            req_wise.sender = cached_sender;
            req_wise.is_common = (handle->common_stream_id &&
                                  *handle->common_stream_id == target_id);
            handle->tx_router[target_id] = std::move(req_wise);
        }
    }

    spdlog::debug("LocalSessionManager: resumed stream for session {} from event {}",
        *id, last_event_id.to_string());

    co_return std::move(stream);
}

// =============================================================================
// route_outgoing() — THE CORE ROUTING LOGIC
// =============================================================================

void LocalSessionManager::route_outgoing(
    std::shared_ptr<LocalSessionHandle> handle,
    TxJsonRpcMessage<RoleServer> msg) {

    std::lock_guard<std::mutex> lock(handle->mutex);

    if (handle->closed) return;

    // Serialize the message to JSON for the SSE event
    json msg_json = msg;

    // Determine the target SSE stream based on message type
    std::shared_ptr<CachedSender> target_sender;

    // 1. Response or Error: route by JSON-RPC request ID
    auto outgoing_id = extract_outgoing_id(msg);
    if (outgoing_id) {
        ResourceKey key{*outgoing_id};
        auto route_it = handle->resource_router.find(key);
        if (route_it != handle->resource_router.end()) {
            auto http_req_id = route_it->second;
            auto tx_it = handle->tx_router.find(http_req_id);
            if (tx_it != handle->tx_router.end() && tx_it->second.sender) {
                target_sender = tx_it->second.sender;
            }
            // Remove the routing entry since the response has been delivered
            handle->resource_router.erase(route_it);

            spdlog::trace("LocalSessionManager: routed response id={} -> http_req {}",
                outgoing_id->to_string(), http_req_id);
        }
    }

    // 2. Progress notification: route by progress token
    if (!target_sender) {
        auto progress_token = extract_notification_progress_token(msg);
        if (progress_token) {
            ResourceKey key{progress_token->value};
            auto route_it = handle->resource_router.find(key);
            if (route_it != handle->resource_router.end()) {
                auto http_req_id = route_it->second;
                auto tx_it = handle->tx_router.find(http_req_id);
                if (tx_it != handle->tx_router.end() && tx_it->second.sender) {
                    target_sender = tx_it->second.sender;
                }
                // Don't remove progress token routing — more progress events may follow
                spdlog::trace("LocalSessionManager: routed progress -> http_req {}",
                    http_req_id);
            }
        }
    }

    // 3. Server-initiated requests/notifications: use common stream
    if (!target_sender) {
        if (handle->common_stream_id) {
            auto tx_it = handle->tx_router.find(*handle->common_stream_id);
            if (tx_it != handle->tx_router.end() && tx_it->second.sender) {
                target_sender = tx_it->second.sender;
                spdlog::trace("LocalSessionManager: routed server message -> common stream");
            }
        }
    }

    // 4. Fallback: if still no target, try to find any open stream
    if (!target_sender) {
        for (auto& [req_id, req_wise] : handle->tx_router) {
            if (req_wise.sender) {
                target_sender = req_wise.sender;
                spdlog::trace("LocalSessionManager: routed message -> fallback stream {}",
                    req_id);
                break;
            }
        }
    }

    if (!target_sender) {
        spdlog::warn("LocalSessionManager: no route for outgoing message in session {}",
            *handle->id);
        return;
    }

    // Build the SSE event and send it
    auto event_index = handle->next_event_index.fetch_add(1);

    // Determine the http_request_id for the EventId
    int64_t event_http_req_id = 0;
    for (auto& [req_id, req_wise] : handle->tx_router) {
        if (req_wise.sender.get() == target_sender.get()) {
            event_http_req_id = req_id;
            break;
        }
    }

    ServerSseMessage sse_msg;
    sse_msg.event_id = EventId{event_index, event_http_req_id};
    sse_msg.message = std::move(msg_json);

    target_sender->send(std::move(sse_msg));
}

} // namespace mcp

#endif // MCP_HTTP_TRANSPORT
