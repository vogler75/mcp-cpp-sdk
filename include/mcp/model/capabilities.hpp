#pragma once

#include <map>
#include <optional>
#include <string>

#include "mcp/model/types.hpp"

namespace mcp {

// =============================================================================
// Capability Type Aliases
// =============================================================================

using ExperimentalCapabilities = std::map<std::string, JsonObject>;
using ExtensionCapabilities = std::map<std::string, JsonObject>;

// =============================================================================
// PromptsCapability
// =============================================================================

struct PromptsCapability {
    std::optional<bool> list_changed;

    bool operator==(const PromptsCapability& other) const {
        return list_changed == other.list_changed;
    }

    friend void to_json(json& j, const PromptsCapability& c) {
        j = json::object();
        detail::set_opt(j, "listChanged", c.list_changed);
    }

    friend void from_json(const json& j, PromptsCapability& c) {
        detail::get_opt(j, "listChanged", c.list_changed);
    }
};

// =============================================================================
// ResourcesCapability
// =============================================================================

struct ResourcesCapability {
    std::optional<bool> subscribe;
    std::optional<bool> list_changed;

    bool operator==(const ResourcesCapability& other) const {
        return subscribe == other.subscribe && list_changed == other.list_changed;
    }

    friend void to_json(json& j, const ResourcesCapability& c) {
        j = json::object();
        detail::set_opt(j, "subscribe", c.subscribe);
        detail::set_opt(j, "listChanged", c.list_changed);
    }

    friend void from_json(const json& j, ResourcesCapability& c) {
        detail::get_opt(j, "subscribe", c.subscribe);
        detail::get_opt(j, "listChanged", c.list_changed);
    }
};

// =============================================================================
// ToolsCapability
// =============================================================================

struct ToolsCapability {
    std::optional<bool> list_changed;

    bool operator==(const ToolsCapability& other) const {
        return list_changed == other.list_changed;
    }

    friend void to_json(json& j, const ToolsCapability& c) {
        j = json::object();
        detail::set_opt(j, "listChanged", c.list_changed);
    }

    friend void from_json(const json& j, ToolsCapability& c) {
        detail::get_opt(j, "listChanged", c.list_changed);
    }
};

// =============================================================================
// RootsCapabilities
// =============================================================================

struct RootsCapabilities {
    std::optional<bool> list_changed;

    bool operator==(const RootsCapabilities& other) const {
        return list_changed == other.list_changed;
    }

    friend void to_json(json& j, const RootsCapabilities& c) {
        j = json::object();
        detail::set_opt(j, "listChanged", c.list_changed);
    }

    friend void from_json(const json& j, RootsCapabilities& c) {
        detail::get_opt(j, "listChanged", c.list_changed);
    }
};

// =============================================================================
// Task-related Capabilities
// =============================================================================

struct SamplingTaskCapability {
    std::optional<JsonObject> create_message;

    bool operator==(const SamplingTaskCapability& other) const {
        return create_message == other.create_message;
    }

    friend void to_json(json& j, const SamplingTaskCapability& c) {
        j = json::object();
        detail::set_opt(j, "createMessage", c.create_message);
    }

    friend void from_json(const json& j, SamplingTaskCapability& c) {
        detail::get_opt(j, "createMessage", c.create_message);
    }
};

struct ElicitationTaskCapability {
    std::optional<JsonObject> create;

    bool operator==(const ElicitationTaskCapability& other) const {
        return create == other.create;
    }

    friend void to_json(json& j, const ElicitationTaskCapability& c) {
        j = json::object();
        detail::set_opt(j, "create", c.create);
    }

    friend void from_json(const json& j, ElicitationTaskCapability& c) {
        detail::get_opt(j, "create", c.create);
    }
};

struct ToolsTaskCapability {
    std::optional<JsonObject> call;

    bool operator==(const ToolsTaskCapability& other) const {
        return call == other.call;
    }

    friend void to_json(json& j, const ToolsTaskCapability& c) {
        j = json::object();
        detail::set_opt(j, "call", c.call);
    }

    friend void from_json(const json& j, ToolsTaskCapability& c) {
        detail::get_opt(j, "call", c.call);
    }
};

struct TaskRequestsCapability {
    std::optional<SamplingTaskCapability> sampling;
    std::optional<ElicitationTaskCapability> elicitation;
    std::optional<ToolsTaskCapability> tools;

    bool operator==(const TaskRequestsCapability& other) const {
        return sampling == other.sampling && elicitation == other.elicitation
            && tools == other.tools;
    }

    friend void to_json(json& j, const TaskRequestsCapability& c) {
        j = json::object();
        detail::set_opt(j, "sampling", c.sampling);
        detail::set_opt(j, "elicitation", c.elicitation);
        detail::set_opt(j, "tools", c.tools);
    }

    friend void from_json(const json& j, TaskRequestsCapability& c) {
        detail::get_opt(j, "sampling", c.sampling);
        detail::get_opt(j, "elicitation", c.elicitation);
        detail::get_opt(j, "tools", c.tools);
    }
};

// =============================================================================
// TasksCapability
// =============================================================================

struct TasksCapability {
    std::optional<TaskRequestsCapability> requests;
    std::optional<JsonObject> list;
    std::optional<JsonObject> cancel;

    bool operator==(const TasksCapability& other) const {
        return requests == other.requests && list == other.list
            && cancel == other.cancel;
    }

    // Helper methods
    bool supports_list() const { return list.has_value(); }
    bool supports_cancel() const { return cancel.has_value(); }

    bool supports_tools_call() const {
        return requests.has_value()
            && requests->tools.has_value()
            && requests->tools->call.has_value();
    }

    bool supports_sampling_create_message() const {
        return requests.has_value()
            && requests->sampling.has_value()
            && requests->sampling->create_message.has_value();
    }

    bool supports_elicitation_create() const {
        return requests.has_value()
            && requests->elicitation.has_value()
            && requests->elicitation->create.has_value();
    }

    /// Default client tasks capability with sampling and elicitation support.
    static TasksCapability client_default() {
        TasksCapability t;
        t.list = JsonObject{};
        t.cancel = JsonObject{};
        SamplingTaskCapability stc;
        stc.create_message = JsonObject{};
        ElicitationTaskCapability etc;
        etc.create = JsonObject{};
        TaskRequestsCapability req;
        req.sampling = std::move(stc);
        req.elicitation = std::move(etc);
        t.requests = std::move(req);
        return t;
    }

    /// Default server tasks capability with tools/call support.
    static TasksCapability server_default() {
        TasksCapability t;
        t.list = JsonObject{};
        t.cancel = JsonObject{};
        ToolsTaskCapability ttc;
        ttc.call = JsonObject{};
        TaskRequestsCapability req;
        req.tools = std::move(ttc);
        t.requests = std::move(req);
        return t;
    }

    friend void to_json(json& j, const TasksCapability& c) {
        j = json::object();
        detail::set_opt(j, "requests", c.requests);
        detail::set_opt(j, "list", c.list);
        detail::set_opt(j, "cancel", c.cancel);
    }

    friend void from_json(const json& j, TasksCapability& c) {
        detail::get_opt(j, "requests", c.requests);
        detail::get_opt(j, "list", c.list);
        detail::get_opt(j, "cancel", c.cancel);
    }
};

// =============================================================================
// ElicitationCapability
// =============================================================================

struct ElicitationCapability {
    std::optional<bool> schema_validation;

    bool operator==(const ElicitationCapability& other) const {
        return schema_validation == other.schema_validation;
    }

    friend void to_json(json& j, const ElicitationCapability& c) {
        j = json::object();
        detail::set_opt(j, "schemaValidation", c.schema_validation);
    }

    friend void from_json(const json& j, ElicitationCapability& c) {
        detail::get_opt(j, "schemaValidation", c.schema_validation);
    }
};

// =============================================================================
// SamplingCapability
// =============================================================================

struct SamplingCapability {
    std::optional<JsonObject> tools;
    std::optional<JsonObject> context;

    bool operator==(const SamplingCapability& other) const {
        return tools == other.tools && context == other.context;
    }

    friend void to_json(json& j, const SamplingCapability& c) {
        j = json::object();
        detail::set_opt(j, "tools", c.tools);
        detail::set_opt(j, "context", c.context);
    }

    friend void from_json(const json& j, SamplingCapability& c) {
        detail::get_opt(j, "tools", c.tools);
        detail::get_opt(j, "context", c.context);
    }
};

// =============================================================================
// ClientCapabilities
// =============================================================================

struct ClientCapabilities {
    std::optional<ExperimentalCapabilities> experimental;
    std::optional<ExtensionCapabilities> extensions;
    std::optional<RootsCapabilities> roots;
    std::optional<SamplingCapability> sampling;
    std::optional<ElicitationCapability> elicitation;
    std::optional<TasksCapability> tasks;

    bool operator==(const ClientCapabilities& other) const {
        return experimental == other.experimental && extensions == other.extensions
            && roots == other.roots && sampling == other.sampling
            && elicitation == other.elicitation && tasks == other.tasks;
    }

    class Builder;
    static Builder builder();

    friend void to_json(json& j, const ClientCapabilities& c) {
        j = json::object();
        detail::set_opt(j, "experimental", c.experimental);
        detail::set_opt(j, "extensions", c.extensions);
        detail::set_opt(j, "roots", c.roots);
        detail::set_opt(j, "sampling", c.sampling);
        detail::set_opt(j, "elicitation", c.elicitation);
        detail::set_opt(j, "tasks", c.tasks);
    }

    friend void from_json(const json& j, ClientCapabilities& c) {
        detail::get_opt(j, "experimental", c.experimental);
        detail::get_opt(j, "extensions", c.extensions);
        detail::get_opt(j, "roots", c.roots);
        detail::get_opt(j, "sampling", c.sampling);
        detail::get_opt(j, "elicitation", c.elicitation);
        detail::get_opt(j, "tasks", c.tasks);
    }
};

// =============================================================================
// ServerCapabilities
// =============================================================================

struct ServerCapabilities {
    std::optional<ExperimentalCapabilities> experimental;
    std::optional<ExtensionCapabilities> extensions;
    std::optional<JsonObject> logging;
    std::optional<JsonObject> completions;
    std::optional<PromptsCapability> prompts;
    std::optional<ResourcesCapability> resources;
    std::optional<ToolsCapability> tools;
    std::optional<TasksCapability> tasks;

    bool operator==(const ServerCapabilities& other) const {
        return experimental == other.experimental && extensions == other.extensions
            && logging == other.logging && completions == other.completions
            && prompts == other.prompts && resources == other.resources
            && tools == other.tools && tasks == other.tasks;
    }

    class Builder;
    static Builder builder();

    friend void to_json(json& j, const ServerCapabilities& c) {
        j = json::object();
        detail::set_opt(j, "experimental", c.experimental);
        detail::set_opt(j, "extensions", c.extensions);
        detail::set_opt(j, "logging", c.logging);
        detail::set_opt(j, "completions", c.completions);
        detail::set_opt(j, "prompts", c.prompts);
        detail::set_opt(j, "resources", c.resources);
        detail::set_opt(j, "tools", c.tools);
        detail::set_opt(j, "tasks", c.tasks);
    }

    friend void from_json(const json& j, ServerCapabilities& c) {
        detail::get_opt(j, "experimental", c.experimental);
        detail::get_opt(j, "extensions", c.extensions);
        detail::get_opt(j, "logging", c.logging);
        detail::get_opt(j, "completions", c.completions);
        detail::get_opt(j, "prompts", c.prompts);
        detail::get_opt(j, "resources", c.resources);
        detail::get_opt(j, "tools", c.tools);
        detail::get_opt(j, "tasks", c.tasks);
    }
};

// =============================================================================
// ClientCapabilities::Builder
// =============================================================================

class ClientCapabilities::Builder {
public:
    Builder() = default;

    Builder& enable_experimental() {
        caps_.experimental = ExperimentalCapabilities{};
        return *this;
    }
    Builder& enable_experimental_with(ExperimentalCapabilities val) {
        caps_.experimental = std::move(val);
        return *this;
    }

    Builder& enable_extensions() {
        caps_.extensions = ExtensionCapabilities{};
        return *this;
    }
    Builder& enable_extensions_with(ExtensionCapabilities val) {
        caps_.extensions = std::move(val);
        return *this;
    }

    Builder& enable_roots() {
        caps_.roots = RootsCapabilities{};
        return *this;
    }
    Builder& enable_roots_with(RootsCapabilities val) {
        caps_.roots = std::move(val);
        return *this;
    }
    Builder& enable_roots_list_changed() {
        if (!caps_.roots.has_value()) {
            caps_.roots = RootsCapabilities{};
        }
        caps_.roots->list_changed = true;
        return *this;
    }

    Builder& enable_sampling() {
        caps_.sampling = SamplingCapability{};
        return *this;
    }
    Builder& enable_sampling_with(SamplingCapability val) {
        caps_.sampling = std::move(val);
        return *this;
    }
    Builder& enable_sampling_tools() {
        if (!caps_.sampling.has_value()) {
            caps_.sampling = SamplingCapability{};
        }
        caps_.sampling->tools = JsonObject{};
        return *this;
    }
    Builder& enable_sampling_context() {
        if (!caps_.sampling.has_value()) {
            caps_.sampling = SamplingCapability{};
        }
        caps_.sampling->context = JsonObject{};
        return *this;
    }

    Builder& enable_elicitation() {
        caps_.elicitation = ElicitationCapability{};
        return *this;
    }
    Builder& enable_elicitation_with(ElicitationCapability val) {
        caps_.elicitation = std::move(val);
        return *this;
    }
    Builder& enable_elicitation_schema_validation() {
        if (!caps_.elicitation.has_value()) {
            caps_.elicitation = ElicitationCapability{};
        }
        caps_.elicitation->schema_validation = true;
        return *this;
    }

    Builder& enable_tasks() {
        caps_.tasks = TasksCapability{};
        return *this;
    }
    Builder& enable_tasks_with(TasksCapability val) {
        caps_.tasks = std::move(val);
        return *this;
    }

    ClientCapabilities build() { return std::move(caps_); }

private:
    ClientCapabilities caps_;
};

inline ClientCapabilities::Builder ClientCapabilities::builder() {
    return Builder{};
}

// =============================================================================
// ServerCapabilities::Builder
// =============================================================================

class ServerCapabilities::Builder {
public:
    Builder() = default;

    Builder& enable_experimental() {
        caps_.experimental = ExperimentalCapabilities{};
        return *this;
    }
    Builder& enable_experimental_with(ExperimentalCapabilities val) {
        caps_.experimental = std::move(val);
        return *this;
    }

    Builder& enable_extensions() {
        caps_.extensions = ExtensionCapabilities{};
        return *this;
    }
    Builder& enable_extensions_with(ExtensionCapabilities val) {
        caps_.extensions = std::move(val);
        return *this;
    }

    Builder& enable_logging() {
        caps_.logging = JsonObject{};
        return *this;
    }
    Builder& enable_logging_with(JsonObject val) {
        caps_.logging = std::move(val);
        return *this;
    }

    Builder& enable_completions() {
        caps_.completions = JsonObject{};
        return *this;
    }
    Builder& enable_completions_with(JsonObject val) {
        caps_.completions = std::move(val);
        return *this;
    }

    Builder& enable_prompts() {
        caps_.prompts = PromptsCapability{};
        return *this;
    }
    Builder& enable_prompts_with(PromptsCapability val) {
        caps_.prompts = std::move(val);
        return *this;
    }
    Builder& enable_prompts_list_changed() {
        if (!caps_.prompts.has_value()) {
            caps_.prompts = PromptsCapability{};
        }
        caps_.prompts->list_changed = true;
        return *this;
    }

    Builder& enable_resources() {
        caps_.resources = ResourcesCapability{};
        return *this;
    }
    Builder& enable_resources_with(ResourcesCapability val) {
        caps_.resources = std::move(val);
        return *this;
    }
    Builder& enable_resources_list_changed() {
        if (!caps_.resources.has_value()) {
            caps_.resources = ResourcesCapability{};
        }
        caps_.resources->list_changed = true;
        return *this;
    }
    Builder& enable_resources_subscribe() {
        if (!caps_.resources.has_value()) {
            caps_.resources = ResourcesCapability{};
        }
        caps_.resources->subscribe = true;
        return *this;
    }

    Builder& enable_tools() {
        caps_.tools = ToolsCapability{};
        return *this;
    }
    Builder& enable_tools_with(ToolsCapability val) {
        caps_.tools = std::move(val);
        return *this;
    }
    Builder& enable_tool_list_changed() {
        if (!caps_.tools.has_value()) {
            caps_.tools = ToolsCapability{};
        }
        caps_.tools->list_changed = true;
        return *this;
    }

    Builder& enable_tasks() {
        caps_.tasks = TasksCapability{};
        return *this;
    }
    Builder& enable_tasks_with(TasksCapability val) {
        caps_.tasks = std::move(val);
        return *this;
    }

    ServerCapabilities build() { return std::move(caps_); }

private:
    ServerCapabilities caps_;
};

inline ServerCapabilities::Builder ServerCapabilities::builder() {
    return Builder{};
}

} // namespace mcp
