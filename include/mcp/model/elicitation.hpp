#pragma once

#include <optional>
#include <string>
#include <vector>

#include "mcp/model/types.hpp"
#include "mcp/model/meta.hpp"

namespace mcp {

// =============================================================================
// ElicitationAction
// =============================================================================

enum class ElicitationAction {
    Accept,
    Decline,
    Cancel,
};

inline void to_json(json& j, const ElicitationAction& a) {
    switch (a) {
        case ElicitationAction::Accept: j = "accept"; break;
        case ElicitationAction::Decline: j = "decline"; break;
        case ElicitationAction::Cancel: j = "cancel"; break;
    }
}

inline void from_json(const json& j, ElicitationAction& a) {
    auto s = j.get<std::string>();
    if (s == "accept") {
        a = ElicitationAction::Accept;
    } else if (s == "decline") {
        a = ElicitationAction::Decline;
    } else if (s == "cancel") {
        a = ElicitationAction::Cancel;
    } else {
        throw json::other_error::create(501, "Unknown ElicitationAction: " + s, &j);
    }
}

// =============================================================================
// PrimitiveSchema
//
// For now, represented as a raw JSON object describing the schema.
// =============================================================================

using PrimitiveSchema = json;

// =============================================================================
// ElicitationSchema
// =============================================================================

class ElicitationSchemaBuilder;

/// Type-safe elicitation schema for requesting structured user input.
/// Represented as a JSON object with type "object" and primitive-typed properties.
struct ElicitationSchema {
    json value;

    ElicitationSchema() : value(json::object()) {
        value["type"] = "object";
        value["properties"] = json::object();
    }

    explicit ElicitationSchema(json v) : value(std::move(v)) {}

    static ElicitationSchemaBuilder builder();

    bool operator==(const ElicitationSchema& other) const { return value == other.value; }

    friend void to_json(json& j, const ElicitationSchema& s) { j = s.value; }
    friend void from_json(const json& j, ElicitationSchema& s) { s.value = j; }
};

// =============================================================================
// ElicitationSchemaBuilder
// =============================================================================

class ElicitationSchemaBuilder {
public:
    ElicitationSchemaBuilder() {
        schema_["type"] = "object";
        schema_["properties"] = json::object();
    }

    /// Add a required string property.
    ElicitationSchemaBuilder& required_string(const std::string& name) {
        schema_["properties"][name] = json{{"type", "string"}};
        required_.push_back(name);
        return *this;
    }

    /// Add an optional string property.
    ElicitationSchemaBuilder& optional_string(const std::string& name) {
        schema_["properties"][name] = json{{"type", "string"}};
        return *this;
    }

    /// Add a required number property.
    ElicitationSchemaBuilder& required_number(const std::string& name) {
        schema_["properties"][name] = json{{"type", "number"}};
        required_.push_back(name);
        return *this;
    }

    /// Add a required boolean property.
    ElicitationSchemaBuilder& required_boolean(const std::string& name) {
        schema_["properties"][name] = json{{"type", "boolean"}};
        required_.push_back(name);
        return *this;
    }

    /// Add a required email property with a title.
    ElicitationSchemaBuilder& required_email(
        const std::string& name,
        const std::string& title = ""
    ) {
        json prop = {{"type", "string"}, {"format", "email"}};
        if (!title.empty()) {
            prop["title"] = title;
        }
        schema_["properties"][name] = std::move(prop);
        required_.push_back(name);
        return *this;
    }

    /// Build the final ElicitationSchema.
    ElicitationSchema build() {
        if (!required_.empty()) {
            schema_["required"] = required_;
        }
        return ElicitationSchema(schema_);
    }

private:
    json schema_;
    std::vector<std::string> required_;
};

inline ElicitationSchemaBuilder ElicitationSchema::builder() {
    return ElicitationSchemaBuilder();
}

// =============================================================================
// CreateElicitationRequestParams
// =============================================================================

struct CreateElicitationRequestParams {
    std::optional<Meta> meta;
    std::string message;
    ElicitationSchema requested_schema;

    bool operator==(const CreateElicitationRequestParams& other) const {
        return meta == other.meta && message == other.message
            && requested_schema == other.requested_schema;
    }

    friend void to_json(json& j, const CreateElicitationRequestParams& p) {
        j = json{{"message", p.message}, {"requestedSchema", p.requested_schema}};
        if (p.meta.has_value()) {
            j["_meta"] = *p.meta;
        }
    }

    friend void from_json(const json& j, CreateElicitationRequestParams& p) {
        j.at("message").get_to(p.message);
        j.at("requestedSchema").get_to(p.requested_schema);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            p.meta = j["_meta"].get<Meta>();
        }
    }
};

// =============================================================================
// CreateElicitationResult
// =============================================================================

struct CreateElicitationResult {
    ElicitationAction action;
    std::optional<json> content;
    std::optional<Meta> meta;

    bool operator==(const CreateElicitationResult& other) const {
        return action == other.action && content == other.content && meta == other.meta;
    }

    friend void to_json(json& j, const CreateElicitationResult& r) {
        j = json{{"action", r.action}};
        if (r.content.has_value()) {
            j["content"] = *r.content;
        }
        if (r.meta.has_value()) {
            j["_meta"] = *r.meta;
        }
    }

    friend void from_json(const json& j, CreateElicitationResult& r) {
        j.at("action").get_to(r.action);
        if (j.contains("content") && !j["content"].is_null()) {
            r.content = j["content"];
        }
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            r.meta = j["_meta"].get<Meta>();
        }
    }
};

} // namespace mcp
