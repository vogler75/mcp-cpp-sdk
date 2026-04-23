#pragma once

#include <functional>
#include <memory>
#include <string>

namespace mcp {

// =============================================================================
// SessionId — shared reference-counted session identifier
// =============================================================================

/// Session identifier, analogous to Rust's Arc<str>.
/// Using shared_ptr<const std::string> so it can be cheaply copied and stored
/// in multiple data structures (maps, session handles, etc.).
using SessionId = std::shared_ptr<const std::string>;

/// Create a new SessionId from a string.
inline SessionId make_session_id(std::string value) {
    return std::make_shared<const std::string>(std::move(value));
}

} // namespace mcp

// Hash support for use in unordered containers
namespace std {

template <>
struct hash<mcp::SessionId> {
    size_t operator()(const mcp::SessionId& id) const {
        if (!id) return 0;
        return hash<string>{}(*id);
    }
};

} // namespace std
