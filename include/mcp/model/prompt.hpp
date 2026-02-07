#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "mcp/model/content.hpp"
#include "mcp/model/resource.hpp"
#include "mcp/model/types.hpp"

namespace mcp {

// =============================================================================
// PromptArgument
// =============================================================================

struct PromptArgument {
    std::string name;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<bool> required;

    bool operator==(const PromptArgument& other) const {
        return name == other.name && title == other.title
            && description == other.description && required == other.required;
    }

    friend void to_json(json& j, const PromptArgument& a) {
        j = json{{"name", a.name}};
        detail::set_opt(j, "title", a.title);
        detail::set_opt(j, "description", a.description);
        detail::set_opt(j, "required", a.required);
    }

    friend void from_json(const json& j, PromptArgument& a) {
        j.at("name").get_to(a.name);
        detail::get_opt(j, "title", a.title);
        detail::get_opt(j, "description", a.description);
        detail::get_opt(j, "required", a.required);
    }
};

// =============================================================================
// Prompt
// =============================================================================

struct Prompt {
    std::string name;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::vector<PromptArgument>> arguments;
    std::optional<std::vector<Icon>> icons;

    Prompt() = default;

    Prompt(std::string name_,
           std::optional<std::string> description_ = std::nullopt,
           std::optional<std::vector<PromptArgument>> arguments_ = std::nullopt)
        : name(std::move(name_))
        , description(std::move(description_))
        , arguments(std::move(arguments_)) {}

    bool operator==(const Prompt& other) const {
        return name == other.name && title == other.title
            && description == other.description && arguments == other.arguments
            && icons == other.icons;
    }

    friend void to_json(json& j, const Prompt& p) {
        j = json{{"name", p.name}};
        detail::set_opt(j, "title", p.title);
        detail::set_opt(j, "description", p.description);
        detail::set_opt(j, "arguments", p.arguments);
        detail::set_opt(j, "icons", p.icons);
    }

    friend void from_json(const json& j, Prompt& p) {
        j.at("name").get_to(p.name);
        detail::get_opt(j, "title", p.title);
        detail::get_opt(j, "description", p.description);
        detail::get_opt(j, "arguments", p.arguments);
        detail::get_opt(j, "icons", p.icons);
    }
};

// RawEmbeddedResource is already defined in content.hpp (included above)

// =============================================================================
// PromptMessageContent
//
// Tagged variant with "type" field, snake_case values:
//   "text"              -> RawTextContent
//   "image"             -> RawImageContent
//   "audio"             -> RawAudioContent
//   "embedded_resource" -> RawEmbeddedResource
// =============================================================================

class PromptMessageContent {
public:
    enum class Tag { Text, Image, Audio, Resource };

    using Variant = std::variant<
        RawTextContent,
        RawImageContent,
        RawAudioContent,
        RawEmbeddedResource>;

    PromptMessageContent() : data_(RawTextContent{}) {}

    explicit PromptMessageContent(RawTextContent text)
        : data_(std::move(text)) {}
    explicit PromptMessageContent(RawImageContent image)
        : data_(std::move(image)) {}
    explicit PromptMessageContent(RawAudioContent audio)
        : data_(std::move(audio)) {}
    explicit PromptMessageContent(RawEmbeddedResource resource)
        : data_(std::move(resource)) {}

    // Factory methods
    static PromptMessageContent text(std::string text_value) {
        RawTextContent t;
        t.text = std::move(text_value);
        return PromptMessageContent(std::move(t));
    }

    Tag tag() const {
        return static_cast<Tag>(data_.index());
    }

    bool is_text() const {
        return std::holds_alternative<RawTextContent>(data_);
    }
    bool is_image() const {
        return std::holds_alternative<RawImageContent>(data_);
    }
    bool is_audio() const {
        return std::holds_alternative<RawAudioContent>(data_);
    }
    bool is_resource() const {
        return std::holds_alternative<RawEmbeddedResource>(data_);
    }

    const RawTextContent& as_text() const {
        return std::get<RawTextContent>(data_);
    }
    const RawImageContent& as_image() const {
        return std::get<RawImageContent>(data_);
    }
    const RawAudioContent& as_audio() const {
        return std::get<RawAudioContent>(data_);
    }
    const RawEmbeddedResource& as_resource() const {
        return std::get<RawEmbeddedResource>(data_);
    }

    bool operator==(const PromptMessageContent& other) const {
        return data_ == other.data_;
    }

    friend void to_json(json& j, const PromptMessageContent& c) {
        std::visit([&j](const auto& v) { j = v; }, c.data_);
        // Add the type tag
        switch (c.tag()) {
            case Tag::Text:     j["type"] = "text"; break;
            case Tag::Image:    j["type"] = "image"; break;
            case Tag::Audio:    j["type"] = "audio"; break;
            case Tag::Resource: j["type"] = "embedded_resource"; break;
        }
    }

    friend void from_json(const json& j, PromptMessageContent& c) {
        auto type = j.at("type").get<std::string>();
        if (type == "text") {
            RawTextContent t;
            from_json(j, t);
            c.data_ = std::move(t);
        } else if (type == "image") {
            RawImageContent img;
            from_json(j, img);
            c.data_ = std::move(img);
        } else if (type == "audio") {
            RawAudioContent aud;
            from_json(j, aud);
            c.data_ = std::move(aud);
        } else if (type == "embedded_resource") {
            RawEmbeddedResource res;
            from_json(j, res);
            c.data_ = std::move(res);
        } else {
            throw json::other_error::create(
                501, "Unknown PromptMessageContent type: " + type, &j);
        }
    }

private:
    Variant data_;
};

// =============================================================================
// PromptMessage
// =============================================================================

struct PromptMessage {
    Role role;
    PromptMessageContent content;

    PromptMessage() : role(Role::User) {}

    PromptMessage(Role role_, PromptMessageContent content_)
        : role(role_), content(std::move(content_)) {}

    /// Create a new text message with the given role
    static PromptMessage new_text(Role role, std::string text) {
        return PromptMessage(role, PromptMessageContent::text(std::move(text)));
    }

    bool operator==(const PromptMessage& other) const {
        return role == other.role && content == other.content;
    }

    friend void to_json(json& j, const PromptMessage& m) {
        j = json{{"role", m.role}, {"content", m.content}};
    }

    friend void from_json(const json& j, PromptMessage& m) {
        j.at("role").get_to(m.role);
        j.at("content").get_to(m.content);
    }
};

// =============================================================================
// GetPromptRequestParams
// =============================================================================

struct GetPromptRequestParams {
    std::optional<Meta> meta;
    std::string name;
    std::optional<JsonObject> arguments;

    bool operator==(const GetPromptRequestParams& other) const {
        return meta == other.meta && name == other.name
            && arguments == other.arguments;
    }

    friend void to_json(json& j, const GetPromptRequestParams& p) {
        j = json{{"name", p.name}};
        if (p.meta.has_value()) {
            j["_meta"] = *p.meta;
        }
        detail::set_opt(j, "arguments", p.arguments);
    }

    friend void from_json(const json& j, GetPromptRequestParams& p) {
        j.at("name").get_to(p.name);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            p.meta = j["_meta"].get<Meta>();
        }
        detail::get_opt(j, "arguments", p.arguments);
    }
};

// =============================================================================
// GetPromptResult
// =============================================================================

struct GetPromptResult {
    std::optional<std::string> description;
    std::vector<PromptMessage> messages;

    bool operator==(const GetPromptResult& other) const {
        return description == other.description && messages == other.messages;
    }

    friend void to_json(json& j, const GetPromptResult& r) {
        j = json{{"messages", r.messages}};
        detail::set_opt(j, "description", r.description);
    }

    friend void from_json(const json& j, GetPromptResult& r) {
        j.at("messages").get_to(r.messages);
        detail::get_opt(j, "description", r.description);
    }
};

// =============================================================================
// ListPromptsResult
// =============================================================================

struct ListPromptsResult {
    std::optional<Meta> meta;
    std::optional<std::string> next_cursor;
    std::vector<Prompt> prompts;

    ListPromptsResult() = default;

    static ListPromptsResult with_all_items(std::vector<Prompt> items) {
        ListPromptsResult r;
        r.prompts = std::move(items);
        return r;
    }

    bool operator==(const ListPromptsResult& other) const {
        return meta == other.meta && next_cursor == other.next_cursor
            && prompts == other.prompts;
    }

    friend void to_json(json& j, const ListPromptsResult& r) {
        j = json{{"prompts", r.prompts}};
        if (r.meta.has_value()) {
            j["_meta"] = *r.meta;
        }
        detail::set_opt(j, "nextCursor", r.next_cursor);
    }

    friend void from_json(const json& j, ListPromptsResult& r) {
        j.at("prompts").get_to(r.prompts);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            r.meta = j["_meta"].get<Meta>();
        }
        detail::get_opt(j, "nextCursor", r.next_cursor);
    }
};

} // namespace mcp
