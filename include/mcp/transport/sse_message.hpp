#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace mcp {

using json = nlohmann::json;

// =============================================================================
// EventId — identifies an SSE event within a session
// =============================================================================

/// Identifies a specific SSE event. The format is "<index>/<http_request_id>".
/// - `index` is a monotonically increasing counter across the session.
/// - `http_request_id` identifies which HTTP request stream owns this event.
struct EventId {
    int64_t index = 0;
    int64_t http_request_id = 0;

    /// Format as "<index>/<http_request_id>" for the SSE `id:` field.
    std::string to_string() const;

    /// Parse from "<index>/<http_request_id>" or just "<index>" (legacy).
    /// Returns nullopt if the string is malformed.
    static std::optional<EventId> parse(std::string_view s);

    bool operator==(const EventId& other) const {
        return index == other.index && http_request_id == other.http_request_id;
    }
    bool operator!=(const EventId& other) const { return !(*this == other); }
    bool operator<(const EventId& other) const { return index < other.index; }
};

// =============================================================================
// ServerSseMessage — a single SSE message to send to a client
// =============================================================================

/// An SSE message ready to be serialized and sent over the wire.
struct ServerSseMessage {
    EventId event_id;

    /// The JSON-RPC message payload. nullopt for priming/keepalive events.
    std::optional<json> message;

    /// Optional retry hint to include in the SSE event.
    std::optional<std::chrono::milliseconds> retry_hint;

    /// Format this message as an SSE text block (id: ...\ndata: ...\n\n).
    std::string format() const;

    /// Create a priming event (empty data with retry hint).
    static ServerSseMessage priming(EventId id, std::chrono::milliseconds retry);

    /// Create a keepalive comment (: keepalive\n\n).
    static std::string keepalive();
};

} // namespace mcp
