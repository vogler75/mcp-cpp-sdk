#pragma once

#include <optional>
#include <string>
#include <vector>

#include "mcp/model/types.hpp"
#include "mcp/model/meta.hpp"
#include "mcp/model/resource.hpp"
#include "mcp/model/tool.hpp"

namespace mcp {

// =============================================================================
// PaginatedRequestParams
// =============================================================================

struct PaginatedRequestParams {
    std::optional<Meta> meta;
    std::optional<std::string> cursor;

    bool operator==(const PaginatedRequestParams& other) const {
        return meta == other.meta && cursor == other.cursor;
    }

    friend void to_json(json& j, const PaginatedRequestParams& p) {
        j = json::object();
        if (p.meta.has_value()) {
            j["_meta"] = *p.meta;
        }
        detail::set_opt(j, "cursor", p.cursor);
    }

    friend void from_json(const json& j, PaginatedRequestParams& p) {
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            p.meta = j["_meta"].get<Meta>();
        }
        detail::get_opt(j, "cursor", p.cursor);
    }
};

// =============================================================================
// ListResourcesResult
// =============================================================================

struct ListResourcesResult {
    std::optional<Meta> meta;
    std::optional<std::string> next_cursor;
    std::vector<Resource> resources;

    bool operator==(const ListResourcesResult& other) const {
        return meta == other.meta && next_cursor == other.next_cursor
            && resources == other.resources;
    }

    friend void to_json(json& j, const ListResourcesResult& r) {
        j = json{{"resources", r.resources}};
        if (r.meta.has_value()) {
            j["_meta"] = *r.meta;
        }
        detail::set_opt(j, "nextCursor", r.next_cursor);
    }

    friend void from_json(const json& j, ListResourcesResult& r) {
        j.at("resources").get_to(r.resources);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            r.meta = j["_meta"].get<Meta>();
        }
        detail::get_opt(j, "nextCursor", r.next_cursor);
    }
};

// =============================================================================
// ListResourceTemplatesResult
// =============================================================================

struct ListResourceTemplatesResult {
    std::optional<Meta> meta;
    std::optional<std::string> next_cursor;
    std::vector<ResourceTemplate> resource_templates;

    bool operator==(const ListResourceTemplatesResult& other) const {
        return meta == other.meta && next_cursor == other.next_cursor
            && resource_templates == other.resource_templates;
    }

    friend void to_json(json& j, const ListResourceTemplatesResult& r) {
        j = json{{"resourceTemplates", r.resource_templates}};
        if (r.meta.has_value()) {
            j["_meta"] = *r.meta;
        }
        detail::set_opt(j, "nextCursor", r.next_cursor);
    }

    friend void from_json(const json& j, ListResourceTemplatesResult& r) {
        j.at("resourceTemplates").get_to(r.resource_templates);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            r.meta = j["_meta"].get<Meta>();
        }
        detail::get_opt(j, "nextCursor", r.next_cursor);
    }
};

// =============================================================================
// ListToolsResult
// =============================================================================

struct ListToolsResult {
    std::optional<Meta> meta;
    std::optional<std::string> next_cursor;
    std::vector<Tool> tools;

    bool operator==(const ListToolsResult& other) const {
        return meta == other.meta && next_cursor == other.next_cursor && tools == other.tools;
    }

    friend void to_json(json& j, const ListToolsResult& r) {
        j = json{{"tools", r.tools}};
        if (r.meta.has_value()) {
            j["_meta"] = *r.meta;
        }
        detail::set_opt(j, "nextCursor", r.next_cursor);
    }

    friend void from_json(const json& j, ListToolsResult& r) {
        j.at("tools").get_to(r.tools);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            r.meta = j["_meta"].get<Meta>();
        }
        detail::get_opt(j, "nextCursor", r.next_cursor);
    }
};

} // namespace mcp
