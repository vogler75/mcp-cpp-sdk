#pragma once

#include <optional>
#include <string>
#include <vector>

#include "mcp/model/types.hpp"

namespace mcp {

// =============================================================================
// Root
// =============================================================================

struct Root {
    std::string uri;
    std::optional<std::string> name;

    bool operator==(const Root& other) const {
        return uri == other.uri && name == other.name;
    }

    friend void to_json(json& j, const Root& r) {
        j = json{{"uri", r.uri}};
        detail::set_opt(j, "name", r.name);
    }

    friend void from_json(const json& j, Root& r) {
        j.at("uri").get_to(r.uri);
        detail::get_opt(j, "name", r.name);
    }
};

// =============================================================================
// ListRootsResult
// =============================================================================

struct ListRootsResult {
    std::vector<Root> roots;

    bool operator==(const ListRootsResult& other) const {
        return roots == other.roots;
    }

    friend void to_json(json& j, const ListRootsResult& r) {
        j = json{{"roots", r.roots}};
    }

    friend void from_json(const json& j, ListRootsResult& r) {
        j.at("roots").get_to(r.roots);
    }
};

} // namespace mcp
