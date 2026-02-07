#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "mcp/model/content.hpp"
#include "mcp/model/meta.hpp"
#include "mcp/model/types.hpp"

namespace mcp {

// =============================================================================
// TaskSupport
// =============================================================================

enum class TaskSupport {
    Forbidden,  // default
    Optional,
    Required,
};

inline void to_json(json& j, const TaskSupport& ts) {
    switch (ts) {
        case TaskSupport::Forbidden: j = "forbidden"; break;
        case TaskSupport::Optional: j = "optional"; break;
        case TaskSupport::Required: j = "required"; break;
    }
}

inline void from_json(const json& j, TaskSupport& ts) {
    auto s = j.get<std::string>();
    if (s == "forbidden") {
        ts = TaskSupport::Forbidden;
    } else if (s == "optional") {
        ts = TaskSupport::Optional;
    } else if (s == "required") {
        ts = TaskSupport::Required;
    } else {
        throw json::other_error::create(501, "Unknown TaskSupport value: " + s, &j);
    }
}

// =============================================================================
// ToolExecution
// =============================================================================

struct ToolExecution {
    std::optional<TaskSupport> task_support;

    ToolExecution() = default;

    ToolExecution with_task_support(TaskSupport ts) && {
        task_support = ts;
        return std::move(*this);
    }

    ToolExecution with_task_support(TaskSupport ts) const & {
        ToolExecution copy = *this;
        copy.task_support = ts;
        return copy;
    }

    bool operator==(const ToolExecution& other) const {
        return task_support == other.task_support;
    }

    friend void to_json(json& j, const ToolExecution& e) {
        j = json::object();
        detail::set_opt(j, "taskSupport", e.task_support);
    }

    friend void from_json(const json& j, ToolExecution& e) {
        detail::get_opt(j, "taskSupport", e.task_support);
    }
};

// =============================================================================
// ToolAnnotations
// =============================================================================

struct ToolAnnotations {
    std::optional<std::string> title;
    std::optional<bool> read_only_hint;
    std::optional<bool> destructive_hint;
    std::optional<bool> idempotent_hint;
    std::optional<bool> open_world_hint;

    ToolAnnotations() = default;

    // -- Builder methods (return by value for chaining) --

    static ToolAnnotations with_title(std::string title_) {
        ToolAnnotations a;
        a.title = std::move(title_);
        return a;
    }

    ToolAnnotations& read_only(bool v) {
        read_only_hint = v;
        return *this;
    }

    ToolAnnotations& destructive(bool v) {
        destructive_hint = v;
        return *this;
    }

    ToolAnnotations& idempotent(bool v) {
        idempotent_hint = v;
        return *this;
    }

    ToolAnnotations& open_world(bool v) {
        open_world_hint = v;
        return *this;
    }

    /// If not set, defaults to true.
    bool is_destructive() const {
        return destructive_hint.value_or(true);
    }

    /// If not set, defaults to false.
    bool is_idempotent() const {
        return idempotent_hint.value_or(false);
    }

    bool operator==(const ToolAnnotations& other) const {
        return title == other.title && read_only_hint == other.read_only_hint
            && destructive_hint == other.destructive_hint
            && idempotent_hint == other.idempotent_hint
            && open_world_hint == other.open_world_hint;
    }

    friend void to_json(json& j, const ToolAnnotations& a) {
        j = json::object();
        detail::set_opt(j, "title", a.title);
        detail::set_opt(j, "readOnlyHint", a.read_only_hint);
        detail::set_opt(j, "destructiveHint", a.destructive_hint);
        detail::set_opt(j, "idempotentHint", a.idempotent_hint);
        detail::set_opt(j, "openWorldHint", a.open_world_hint);
    }

    friend void from_json(const json& j, ToolAnnotations& a) {
        detail::get_opt(j, "title", a.title);
        detail::get_opt(j, "readOnlyHint", a.read_only_hint);
        detail::get_opt(j, "destructiveHint", a.destructive_hint);
        detail::get_opt(j, "idempotentHint", a.idempotent_hint);
        detail::get_opt(j, "openWorldHint", a.open_world_hint);
    }
};

// =============================================================================
// Tool
// =============================================================================

struct Tool {
    std::string name;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::shared_ptr<JsonObject> input_schema;
    std::optional<std::shared_ptr<JsonObject>> output_schema;
    std::optional<ToolAnnotations> annotations;
    std::optional<ToolExecution> execution;
    std::optional<std::vector<Icon>> icons;
    std::optional<Meta> meta;

    Tool() = default;

    /// Primary factory: create a tool with name, description, and input schema
    static Tool create(
        std::string name_,
        std::string description_,
        std::shared_ptr<JsonObject> input_schema_
    ) {
        Tool t;
        t.name = std::move(name_);
        t.description = std::move(description_);
        t.input_schema = std::move(input_schema_);
        return t;
    }

    /// Builder: set annotations
    Tool& annotate(ToolAnnotations ann) {
        annotations = std::move(ann);
        return *this;
    }

    /// Builder: set execution configuration
    Tool& with_execution(ToolExecution exec) {
        execution = std::move(exec);
        return *this;
    }

    /// Returns the task support mode, defaulting to Forbidden if unset
    TaskSupport task_support() const {
        if (execution.has_value() && execution->task_support.has_value()) {
            return *execution->task_support;
        }
        return TaskSupport::Forbidden;
    }

    bool operator==(const Tool& other) const {
        // Compare shared_ptr contents rather than addresses
        bool schema_eq = false;
        if (input_schema && other.input_schema) {
            schema_eq = *input_schema == *other.input_schema;
        } else {
            schema_eq = (!input_schema && !other.input_schema);
        }

        bool out_schema_eq = false;
        if (output_schema.has_value() && other.output_schema.has_value()) {
            if (*output_schema && *other.output_schema) {
                out_schema_eq = **output_schema == **other.output_schema;
            } else {
                out_schema_eq = (!*output_schema && !*other.output_schema);
            }
        } else {
            out_schema_eq = (!output_schema.has_value() && !other.output_schema.has_value());
        }

        return name == other.name && title == other.title
            && description == other.description && schema_eq && out_schema_eq
            && annotations == other.annotations && execution == other.execution
            && icons == other.icons && meta == other.meta;
    }

    friend void to_json(json& j, const Tool& t) {
        j = json{{"name", t.name}};
        detail::set_opt(j, "title", t.title);
        detail::set_opt(j, "description", t.description);

        if (t.input_schema) {
            j["inputSchema"] = *t.input_schema;
        }
        if (t.output_schema.has_value() && *t.output_schema) {
            j["outputSchema"] = **t.output_schema;
        }
        detail::set_opt(j, "annotations", t.annotations);
        detail::set_opt(j, "execution", t.execution);
        detail::set_opt(j, "icons", t.icons);
        if (t.meta.has_value()) {
            j["_meta"] = *t.meta;
        }
    }

    friend void from_json(const json& j, Tool& t) {
        j.at("name").get_to(t.name);
        detail::get_opt(j, "title", t.title);
        detail::get_opt(j, "description", t.description);

        if (j.contains("inputSchema") && !j["inputSchema"].is_null()) {
            t.input_schema = std::make_shared<JsonObject>(
                j["inputSchema"].get<JsonObject>());
        }
        if (j.contains("outputSchema") && !j["outputSchema"].is_null()) {
            t.output_schema = std::make_shared<JsonObject>(
                j["outputSchema"].get<JsonObject>());
        }
        detail::get_opt(j, "annotations", t.annotations);
        detail::get_opt(j, "execution", t.execution);
        detail::get_opt(j, "icons", t.icons);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            t.meta = j["_meta"].get<Meta>();
        }
    }
};

// =============================================================================
// CallToolRequestParams
// =============================================================================

struct CallToolRequestParams {
    std::optional<Meta> meta;
    std::string name;
    std::optional<JsonObject> arguments;
    std::optional<JsonObject> task;

    CallToolRequestParams() = default;
    explicit CallToolRequestParams(std::string name_) : name(std::move(name_)) {}

    bool operator==(const CallToolRequestParams& other) const {
        return meta == other.meta && name == other.name
            && arguments == other.arguments && task == other.task;
    }

    friend void to_json(json& j, const CallToolRequestParams& p) {
        j = json{{"name", p.name}};
        if (p.meta.has_value()) {
            j["_meta"] = *p.meta;
        }
        detail::set_opt(j, "arguments", p.arguments);
        detail::set_opt(j, "task", p.task);
    }

    friend void from_json(const json& j, CallToolRequestParams& p) {
        j.at("name").get_to(p.name);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            p.meta = j["_meta"].get<Meta>();
        }
        detail::get_opt(j, "arguments", p.arguments);
        detail::get_opt(j, "task", p.task);
    }
};

// =============================================================================
// CallToolResult
// =============================================================================

struct CallToolResult {
    std::vector<Content> content;
    std::optional<json> structured_content;
    std::optional<bool> is_error;
    std::optional<Meta> meta;

    CallToolResult() = default;

    // -- Static factories --

    /// Create a successful tool result with unstructured content
    static CallToolResult success(std::vector<Content> content_) {
        CallToolResult r;
        r.content = std::move(content_);
        r.is_error = false;
        return r;
    }

    /// Create an error tool result with unstructured content
    static CallToolResult error(std::vector<Content> content_) {
        CallToolResult r;
        r.content = std::move(content_);
        r.is_error = true;
        return r;
    }

    /// Create a successful tool result with structured content.
    /// Also populates content with a text representation.
    static CallToolResult structured(json value) {
        CallToolResult r;
        r.content.push_back(content_factories::text(value.dump()));
        r.structured_content = value;
        r.is_error = false;
        return r;
    }

    /// Create an error tool result with structured content.
    /// Also populates content with a text representation.
    static CallToolResult structured_error(json value) {
        CallToolResult r;
        r.content.push_back(content_factories::text(value.dump()));
        r.structured_content = value;
        r.is_error = true;
        return r;
    }

    bool operator==(const CallToolResult& other) const {
        return content == other.content
            && structured_content == other.structured_content
            && is_error == other.is_error && meta == other.meta;
    }

    friend void to_json(json& j, const CallToolResult& r) {
        j = json::object();
        j["content"] = r.content;
        detail::set_opt(j, "structuredContent", r.structured_content);
        detail::set_opt(j, "isError", r.is_error);
        if (r.meta.has_value()) {
            j["_meta"] = *r.meta;
        }
    }

    friend void from_json(const json& j, CallToolResult& r) {
        // content defaults to empty vector if missing
        if (j.contains("content") && !j["content"].is_null()) {
            j["content"].get_to(r.content);
        }
        detail::get_opt(j, "structuredContent", r.structured_content);
        detail::get_opt(j, "isError", r.is_error);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            r.meta = j["_meta"].get<Meta>();
        }
    }
};

} // namespace mcp
