#pragma once

#ifdef MCP_HTTP_TRANSPORT

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <variant>

#include "mcp/model/types.hpp"
#include "mcp/transport/sse_message.hpp"
#include "mcp/transport/sse_stream.hpp"

namespace mcp {

// =============================================================================
// Types for per-request message routing within a session
// =============================================================================

using HttpRequestId = int64_t;

/// Key for routing outgoing server messages to the correct HTTP request's
/// SSE stream. A server response is routed based on the JSON-RPC request ID
/// it's responding to. Progress notifications are routed based on their
/// progress token.
struct ResourceKey {
    NumberOrString value;

    bool operator==(const ResourceKey& other) const { return value == other.value; }
    bool operator!=(const ResourceKey& other) const { return value != other.value; }
};

} // namespace mcp

namespace std {
template <>
struct hash<mcp::ResourceKey> {
    size_t operator()(const mcp::ResourceKey& k) const {
        return hash<mcp::NumberOrString>{}(k.value);
    }
};
} // namespace std

namespace mcp {

// =============================================================================
// CachedSender — wraps an SseStreamSender with a bounded event cache
// =============================================================================

/// Wraps an SseStreamSender with a bounded cache for event replay.
/// When a client reconnects with a Last-Event-Id, cached events from that
/// point forward can be replayed.
class CachedSender {
public:
    explicit CachedSender(
        std::shared_ptr<SseStreamSender> sender,
        size_t max_cache_size = 64)
        : sender_(std::move(sender))
        , max_cache_size_(max_cache_size) {}

    /// Send a message, caching it for potential replay.
    void send(ServerSseMessage msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sender_) {
            sender_->send(msg);
        }
        // Cache the message
        cache_.push_back(std::move(msg));
        while (cache_.size() > max_cache_size_) {
            cache_.pop_front();
        }
    }

    /// Close the sender.
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sender_) {
            sender_->close();
        }
    }

    /// Replace the underlying sender (for reconnection/resume).
    void replace_sender(std::shared_ptr<SseStreamSender> new_sender) {
        std::lock_guard<std::mutex> lock(mutex_);
        sender_ = std::move(new_sender);
    }

    /// Replay cached events starting from the given event index.
    /// Events with index > start_index are replayed to the given sender.
    void replay_from(int64_t start_index, SseStreamSender& target) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& msg : cache_) {
            if (msg.event_id.index > start_index) {
                target.send(msg);
            }
        }
    }

    /// Get all cached events (for full replay).
    std::vector<ServerSseMessage> get_cached() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {cache_.begin(), cache_.end()};
    }

private:
    std::shared_ptr<SseStreamSender> sender_;
    std::deque<ServerSseMessage> cache_;
    size_t max_cache_size_;
    mutable std::mutex mutex_;
};

// =============================================================================
// HttpRequestWise — per-HTTP-request routing info within a session
// =============================================================================

/// Represents one HTTP request's SSE stream within a session.
struct HttpRequestWise {
    HttpRequestId id;
    std::shared_ptr<CachedSender> sender;
    bool is_common = false;  // true for standalone GET streams
};

} // namespace mcp

#endif // MCP_HTTP_TRANSPORT
