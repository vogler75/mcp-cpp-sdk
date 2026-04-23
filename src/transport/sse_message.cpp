#ifdef MCP_HTTP_TRANSPORT

#include "mcp/transport/sse_message.hpp"

#include <charconv>
#include <sstream>

namespace mcp {

// =============================================================================
// EventId
// =============================================================================

std::string EventId::to_string() const {
    return std::to_string(index) + "/" + std::to_string(http_request_id);
}

std::optional<EventId> EventId::parse(std::string_view s) {
    if (s.empty()) return std::nullopt;

    EventId result;

    auto slash_pos = s.find('/');
    if (slash_pos == std::string_view::npos) {
        // Legacy format: just "<index>"
        auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), result.index);
        if (ec != std::errc{} || ptr != s.data() + s.size()) {
            return std::nullopt;
        }
        result.http_request_id = 0;
    } else {
        // Standard format: "<index>/<http_request_id>"
        auto index_sv = s.substr(0, slash_pos);
        auto req_sv = s.substr(slash_pos + 1);

        auto [ptr1, ec1] = std::from_chars(
            index_sv.data(), index_sv.data() + index_sv.size(), result.index);
        if (ec1 != std::errc{} || ptr1 != index_sv.data() + index_sv.size()) {
            return std::nullopt;
        }

        auto [ptr2, ec2] = std::from_chars(
            req_sv.data(), req_sv.data() + req_sv.size(), result.http_request_id);
        if (ec2 != std::errc{} || ptr2 != req_sv.data() + req_sv.size()) {
            return std::nullopt;
        }
    }

    return result;
}

// =============================================================================
// ServerSseMessage
// =============================================================================

std::string ServerSseMessage::format() const {
    std::string result;

    // Event ID
    result += "id: " + event_id.to_string() + "\n";

    // Retry hint
    if (retry_hint.has_value()) {
        result += "retry: " + std::to_string(retry_hint->count()) + "\n";
    }

    // Data
    if (message.has_value()) {
        result += "data: " + message->dump() + "\n";
    } else {
        result += "data: \n";
    }

    result += "\n";
    return result;
}

ServerSseMessage ServerSseMessage::priming(EventId id, std::chrono::milliseconds retry) {
    ServerSseMessage msg;
    msg.event_id = id;
    msg.message = std::nullopt;
    msg.retry_hint = retry;
    return msg;
}

std::string ServerSseMessage::keepalive() {
    return ": keepalive\n\n";
}

} // namespace mcp

#endif // MCP_HTTP_TRANSPORT
