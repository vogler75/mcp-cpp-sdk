#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "mcp/model/content.hpp"
#include "mcp/model/resource.hpp"
#include "mcp/model/tool.hpp"
#include "mcp/model/types.hpp"

namespace mcp {

// =============================================================================
// ToolChoiceMode
// =============================================================================

enum class ToolChoiceMode {
    Auto,
    Required,
    None,
};

inline void to_json(json& j, const ToolChoiceMode& m) {
    switch (m) {
        case ToolChoiceMode::Auto:     j = "auto"; break;
        case ToolChoiceMode::Required: j = "required"; break;
        case ToolChoiceMode::None:     j = "none"; break;
    }
}

inline void from_json(const json& j, ToolChoiceMode& m) {
    auto s = j.get<std::string>();
    if (s == "auto") {
        m = ToolChoiceMode::Auto;
    } else if (s == "required") {
        m = ToolChoiceMode::Required;
    } else if (s == "none") {
        m = ToolChoiceMode::None;
    } else {
        throw json::other_error::create(
            501, "Unknown ToolChoiceMode value: " + s, &j);
    }
}

// =============================================================================
// ToolChoice
// =============================================================================

struct ToolChoice {
    std::optional<ToolChoiceMode> mode;

    ToolChoice() = default;

    static ToolChoice auto_() {
        ToolChoice tc;
        tc.mode = ToolChoiceMode::Auto;
        return tc;
    }

    static ToolChoice required() {
        ToolChoice tc;
        tc.mode = ToolChoiceMode::Required;
        return tc;
    }

    static ToolChoice none() {
        ToolChoice tc;
        tc.mode = ToolChoiceMode::None;
        return tc;
    }

    bool operator==(const ToolChoice& other) const {
        return mode == other.mode;
    }

    friend void to_json(json& j, const ToolChoice& tc) {
        j = json::object();
        detail::set_opt(j, "mode", tc.mode);
    }

    friend void from_json(const json& j, ToolChoice& tc) {
        detail::get_opt(j, "mode", tc.mode);
    }
};

// =============================================================================
// SamplingContent<T>
//
// Untagged: either a single T value or an array of T values.
// =============================================================================

template <typename T>
class SamplingContent {
public:
    SamplingContent() : data_(std::vector<T>{}) {}

    explicit SamplingContent(T single) : data_(std::move(single)) {}
    explicit SamplingContent(std::vector<T> multiple) : data_(std::move(multiple)) {}

    /// Convert to a vector regardless of whether it's single or multiple
    std::vector<T> into_vec() && {
        if (is_single()) {
            std::vector<T> v;
            v.push_back(std::move(std::get<T>(data_)));
            return v;
        }
        return std::move(std::get<std::vector<T>>(data_));
    }

    /// Convert to a vector (copy)
    std::vector<T> into_vec() const& {
        if (is_single()) {
            return {std::get<T>(data_)};
        }
        return std::get<std::vector<T>>(data_);
    }

    /// Check if the content is empty
    bool is_empty() const {
        if (is_single()) return false;
        return std::get<std::vector<T>>(data_).empty();
    }

    /// Get the number of content items
    size_t len() const {
        if (is_single()) return 1;
        return std::get<std::vector<T>>(data_).size();
    }

    /// Get the first item if present
    const T* first() const {
        if (is_single()) {
            return &std::get<T>(data_);
        }
        const auto& vec = std::get<std::vector<T>>(data_);
        return vec.empty() ? nullptr : &vec.front();
    }

    /// Iterate over all content items
    std::vector<const T*> iter() const {
        std::vector<const T*> result;
        if (is_single()) {
            result.push_back(&std::get<T>(data_));
        } else {
            for (const auto& item : std::get<std::vector<T>>(data_)) {
                result.push_back(&item);
            }
        }
        return result;
    }

    bool operator==(const SamplingContent& other) const {
        return data_ == other.data_;
    }

    friend void to_json(json& j, const SamplingContent& sc) {
        if (sc.is_single()) {
            j = std::get<T>(sc.data_);
        } else {
            j = std::get<std::vector<T>>(sc.data_);
        }
    }

    friend void from_json(const json& j, SamplingContent& sc) {
        if (j.is_array()) {
            sc.data_ = j.get<std::vector<T>>();
        } else {
            sc.data_ = j.get<T>();
        }
    }

private:
    bool is_single() const {
        return std::holds_alternative<T>(data_);
    }

    std::variant<T, std::vector<T>> data_;
};

// =============================================================================
// SamplingMessage
// =============================================================================

struct SamplingMessage {
    Role role;
    SamplingContent<SamplingMessageContent> content;
    std::optional<Meta> meta;

    SamplingMessage() : role(Role::User) {}

    SamplingMessage(Role role_, SamplingContent<SamplingMessageContent> content_,
                    std::optional<Meta> meta_ = std::nullopt)
        : role(role_), content(std::move(content_)), meta(std::move(meta_)) {}

    /// Create a user message with text content
    static SamplingMessage user_text(std::string text) {
        return SamplingMessage(
            Role::User,
            SamplingContent<SamplingMessageContent>(
                SamplingMessageContent::text(std::move(text))));
    }

    /// Create an assistant message with text content
    static SamplingMessage assistant_text(std::string text) {
        return SamplingMessage(
            Role::Assistant,
            SamplingContent<SamplingMessageContent>(
                SamplingMessageContent::text(std::move(text))));
    }

    bool operator==(const SamplingMessage& other) const {
        return role == other.role && content == other.content && meta == other.meta;
    }

    friend void to_json(json& j, const SamplingMessage& m) {
        j = json{{"role", m.role}, {"content", m.content}};
        if (m.meta.has_value()) {
            j["_meta"] = *m.meta;
        }
    }

    friend void from_json(const json& j, SamplingMessage& m) {
        j.at("role").get_to(m.role);
        j.at("content").get_to(m.content);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            m.meta = j["_meta"].get<Meta>();
        }
    }
};

// =============================================================================
// ContextInclusion
// =============================================================================

enum class ContextInclusion {
    AllServers,
    None,
    ThisServer,
};

inline void to_json(json& j, const ContextInclusion& c) {
    switch (c) {
        case ContextInclusion::AllServers:  j = "allServers"; break;
        case ContextInclusion::None:        j = "none"; break;
        case ContextInclusion::ThisServer:  j = "thisServer"; break;
    }
}

inline void from_json(const json& j, ContextInclusion& c) {
    auto s = j.get<std::string>();
    if (s == "allServers") {
        c = ContextInclusion::AllServers;
    } else if (s == "none") {
        c = ContextInclusion::None;
    } else if (s == "thisServer") {
        c = ContextInclusion::ThisServer;
    } else {
        throw json::other_error::create(
            501, "Unknown ContextInclusion value: " + s, &j);
    }
}

// =============================================================================
// ModelHint
// =============================================================================

struct ModelHint {
    std::optional<std::string> name;

    bool operator==(const ModelHint& other) const {
        return name == other.name;
    }

    friend void to_json(json& j, const ModelHint& h) {
        j = json::object();
        detail::set_opt(j, "name", h.name);
    }

    friend void from_json(const json& j, ModelHint& h) {
        detail::get_opt(j, "name", h.name);
    }
};

// =============================================================================
// ModelPreferences
// =============================================================================

struct ModelPreferences {
    std::optional<std::vector<ModelHint>> hints;
    std::optional<float> cost_priority;
    std::optional<float> speed_priority;
    std::optional<float> intelligence_priority;

    bool operator==(const ModelPreferences& other) const {
        return hints == other.hints && cost_priority == other.cost_priority
            && speed_priority == other.speed_priority
            && intelligence_priority == other.intelligence_priority;
    }

    friend void to_json(json& j, const ModelPreferences& p) {
        j = json::object();
        detail::set_opt(j, "hints", p.hints);
        detail::set_opt(j, "costPriority", p.cost_priority);
        detail::set_opt(j, "speedPriority", p.speed_priority);
        detail::set_opt(j, "intelligencePriority", p.intelligence_priority);
    }

    friend void from_json(const json& j, ModelPreferences& p) {
        detail::get_opt(j, "hints", p.hints);
        detail::get_opt(j, "costPriority", p.cost_priority);
        detail::get_opt(j, "speedPriority", p.speed_priority);
        detail::get_opt(j, "intelligencePriority", p.intelligence_priority);
    }
};

// =============================================================================
// CreateMessageRequestParams
// =============================================================================

struct CreateMessageRequestParams {
    std::optional<Meta> meta;
    std::optional<JsonObject> task;
    std::vector<SamplingMessage> messages;
    std::optional<ModelPreferences> model_preferences;
    std::optional<std::string> system_prompt;
    std::optional<ContextInclusion> include_context;
    std::optional<float> temperature;
    uint32_t max_tokens = 0;
    std::optional<std::vector<std::string>> stop_sequences;
    std::optional<json> metadata;
    std::optional<std::vector<Tool>> tools;
    std::optional<ToolChoice> tool_choice;

    bool operator==(const CreateMessageRequestParams& other) const {
        return meta == other.meta && task == other.task
            && messages == other.messages
            && model_preferences == other.model_preferences
            && system_prompt == other.system_prompt
            && include_context == other.include_context
            && temperature == other.temperature
            && max_tokens == other.max_tokens
            && stop_sequences == other.stop_sequences
            && metadata == other.metadata
            && tools == other.tools
            && tool_choice == other.tool_choice;
    }

    friend void to_json(json& j, const CreateMessageRequestParams& p) {
        j = json{
            {"messages", p.messages},
            {"maxTokens", p.max_tokens},
        };
        if (p.meta.has_value()) {
            j["_meta"] = *p.meta;
        }
        detail::set_opt(j, "task", p.task);
        detail::set_opt(j, "modelPreferences", p.model_preferences);
        detail::set_opt(j, "systemPrompt", p.system_prompt);
        detail::set_opt(j, "includeContext", p.include_context);
        detail::set_opt(j, "temperature", p.temperature);
        detail::set_opt(j, "stopSequences", p.stop_sequences);
        detail::set_opt(j, "metadata", p.metadata);
        detail::set_opt(j, "tools", p.tools);
        detail::set_opt(j, "toolChoice", p.tool_choice);
    }

    friend void from_json(const json& j, CreateMessageRequestParams& p) {
        j.at("messages").get_to(p.messages);
        j.at("maxTokens").get_to(p.max_tokens);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            p.meta = j["_meta"].get<Meta>();
        }
        detail::get_opt(j, "task", p.task);
        detail::get_opt(j, "modelPreferences", p.model_preferences);
        detail::get_opt(j, "systemPrompt", p.system_prompt);
        detail::get_opt(j, "includeContext", p.include_context);
        detail::get_opt(j, "temperature", p.temperature);
        detail::get_opt(j, "stopSequences", p.stop_sequences);
        detail::get_opt(j, "metadata", p.metadata);
        detail::get_opt(j, "tools", p.tools);
        detail::get_opt(j, "toolChoice", p.tool_choice);
    }
};

// =============================================================================
// CreateMessageResult
//
// The "message" field is FLATTENED into the top-level JSON object.
// =============================================================================

struct CreateMessageResult {
    std::string model;
    std::optional<std::string> stop_reason;
    SamplingMessage message;

    // Well-known stop reasons
    static constexpr const char* STOP_REASON_END_TURN = "endTurn";
    static constexpr const char* STOP_REASON_END_SEQUENCE = "stopSequence";
    static constexpr const char* STOP_REASON_END_MAX_TOKEN = "maxTokens";
    static constexpr const char* STOP_REASON_TOOL_USE = "toolUse";

    bool operator==(const CreateMessageResult& other) const {
        return model == other.model && stop_reason == other.stop_reason
            && message == other.message;
    }

    friend void to_json(json& j, const CreateMessageResult& r) {
        // Start with the flattened SamplingMessage
        j = r.message;
        // Overlay our own fields
        j["model"] = r.model;
        detail::set_opt(j, "stopReason", r.stop_reason);
    }

    friend void from_json(const json& j, CreateMessageResult& r) {
        j.at("model").get_to(r.model);
        detail::get_opt(j, "stopReason", r.stop_reason);
        // Deserialize the flattened SamplingMessage from the same object
        r.message = j.get<SamplingMessage>();
    }
};

} // namespace mcp
