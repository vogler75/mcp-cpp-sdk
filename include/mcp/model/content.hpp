#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "mcp/model/meta.hpp"
#include "mcp/model/resource.hpp"
#include "mcp/model/types.hpp"

namespace mcp {

// =============================================================================
// RawTextContent
// =============================================================================

struct RawTextContent {
    std::string text;
    std::optional<Meta> meta;

    RawTextContent() = default;
    explicit RawTextContent(std::string text_) : text(std::move(text_)) {}

    bool operator==(const RawTextContent& other) const {
        return text == other.text && meta == other.meta;
    }

    friend void to_json(json& j, const RawTextContent& c) {
        j = json{{"text", c.text}};
        if (c.meta.has_value()) {
            j["_meta"] = *c.meta;
        }
    }

    friend void from_json(const json& j, RawTextContent& c) {
        j.at("text").get_to(c.text);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            c.meta = j["_meta"].get<Meta>();
        }
    }
};

// =============================================================================
// RawImageContent
// =============================================================================

struct RawImageContent {
    std::string data;
    std::string mime_type;
    std::optional<Meta> meta;

    RawImageContent() = default;
    RawImageContent(std::string data_, std::string mime_type_)
        : data(std::move(data_)), mime_type(std::move(mime_type_)) {}

    bool operator==(const RawImageContent& other) const {
        return data == other.data && mime_type == other.mime_type && meta == other.meta;
    }

    friend void to_json(json& j, const RawImageContent& c) {
        j = json{{"data", c.data}, {"mimeType", c.mime_type}};
        if (c.meta.has_value()) {
            j["_meta"] = *c.meta;
        }
    }

    friend void from_json(const json& j, RawImageContent& c) {
        j.at("data").get_to(c.data);
        j.at("mimeType").get_to(c.mime_type);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            c.meta = j["_meta"].get<Meta>();
        }
    }
};

// =============================================================================
// RawAudioContent
// =============================================================================

struct RawAudioContent {
    std::string data;
    std::string mime_type;

    RawAudioContent() = default;
    RawAudioContent(std::string data_, std::string mime_type_)
        : data(std::move(data_)), mime_type(std::move(mime_type_)) {}

    bool operator==(const RawAudioContent& other) const {
        return data == other.data && mime_type == other.mime_type;
    }

    friend void to_json(json& j, const RawAudioContent& c) {
        j = json{{"data", c.data}, {"mimeType", c.mime_type}};
    }

    friend void from_json(const json& j, RawAudioContent& c) {
        j.at("data").get_to(c.data);
        j.at("mimeType").get_to(c.mime_type);
    }
};

// =============================================================================
// RawEmbeddedResource
// =============================================================================

struct RawEmbeddedResource {
    std::optional<Meta> meta;
    ResourceContents resource;

    RawEmbeddedResource() = default;
    explicit RawEmbeddedResource(ResourceContents resource_)
        : resource(std::move(resource_)) {}

    bool operator==(const RawEmbeddedResource& other) const {
        return meta == other.meta && resource == other.resource;
    }

    friend void to_json(json& j, const RawEmbeddedResource& c) {
        j = json{{"resource", c.resource}};
        if (c.meta.has_value()) {
            j["_meta"] = *c.meta;
        }
    }

    friend void from_json(const json& j, RawEmbeddedResource& c) {
        j.at("resource").get_to(c.resource);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            c.meta = j["_meta"].get<Meta>();
        }
    }
};

// =============================================================================
// RawContent
//
// Tagged variant with "type" field using snake_case values:
//   "text", "image", "resource", "audio", "resource_link"
// =============================================================================

class RawContent {
public:
    using Variant = std::variant<
        RawTextContent,
        RawImageContent,
        RawEmbeddedResource,
        RawAudioContent,
        RawResource
    >;

    enum class Tag { Text, Image, Resource, Audio, ResourceLink };

    RawContent() : data_(RawTextContent{}) {}
    RawContent(RawTextContent v) : data_(std::move(v)) {}
    RawContent(RawImageContent v) : data_(std::move(v)) {}
    RawContent(RawEmbeddedResource v) : data_(std::move(v)) {}
    RawContent(RawAudioContent v) : data_(std::move(v)) {}
    RawContent(RawResource v) : data_(std::move(v)) {}

    // -- Tag queries --

    Tag tag() const {
        return static_cast<Tag>(data_.index());
    }

    bool is_text() const { return std::holds_alternative<RawTextContent>(data_); }
    bool is_image() const { return std::holds_alternative<RawImageContent>(data_); }
    bool is_resource() const { return std::holds_alternative<RawEmbeddedResource>(data_); }
    bool is_audio() const { return std::holds_alternative<RawAudioContent>(data_); }
    bool is_resource_link() const { return std::holds_alternative<RawResource>(data_); }

    // -- Accessors --

    const RawTextContent* as_text() const { return std::get_if<RawTextContent>(&data_); }
    RawTextContent* as_text() { return std::get_if<RawTextContent>(&data_); }

    const RawImageContent* as_image() const { return std::get_if<RawImageContent>(&data_); }
    RawImageContent* as_image() { return std::get_if<RawImageContent>(&data_); }

    const RawEmbeddedResource* as_resource() const {
        return std::get_if<RawEmbeddedResource>(&data_);
    }
    RawEmbeddedResource* as_resource() { return std::get_if<RawEmbeddedResource>(&data_); }

    const RawAudioContent* as_audio() const { return std::get_if<RawAudioContent>(&data_); }
    RawAudioContent* as_audio() { return std::get_if<RawAudioContent>(&data_); }

    const RawResource* as_resource_link() const { return std::get_if<RawResource>(&data_); }
    RawResource* as_resource_link() { return std::get_if<RawResource>(&data_); }

    // -- Factories --

    static RawContent text(std::string text_value) {
        return RawContent(RawTextContent(std::move(text_value)));
    }

    static RawContent image(std::string data, std::string mime_type) {
        return RawContent(RawImageContent(std::move(data), std::move(mime_type)));
    }

    static RawContent resource(ResourceContents res) {
        return RawContent(RawEmbeddedResource(std::move(res)));
    }

    static RawContent audio(std::string data, std::string mime_type) {
        return RawContent(RawAudioContent{std::move(data), std::move(mime_type)});
    }

    static RawContent resource_link(RawResource raw_resource) {
        return RawContent(std::move(raw_resource));
    }

    static RawContent embedded_text(std::string uri, std::string content) {
        return RawContent(RawEmbeddedResource(
            ResourceContents::text(std::move(content), std::move(uri))));
    }

    bool operator==(const RawContent& other) const { return data_ == other.data_; }

    friend void to_json(json& j, const RawContent& c) {
        std::visit([&j](const auto& v) { j = v; }, c.data_);
        // Add the type tag
        static const char* tags[] = {"text", "image", "resource", "audio", "resource_link"};
        j["type"] = tags[c.data_.index()];
    }

    friend void from_json(const json& j, RawContent& c) {
        auto type_str = j.at("type").get<std::string>();
        if (type_str == "text") {
            RawTextContent v;
            from_json(j, v);
            c.data_ = std::move(v);
        } else if (type_str == "image") {
            RawImageContent v;
            from_json(j, v);
            c.data_ = std::move(v);
        } else if (type_str == "resource") {
            RawEmbeddedResource v;
            from_json(j, v);
            c.data_ = std::move(v);
        } else if (type_str == "audio") {
            RawAudioContent v;
            from_json(j, v);
            c.data_ = std::move(v);
        } else if (type_str == "resource_link") {
            RawResource v;
            from_json(j, v);
            c.data_ = std::move(v);
        } else {
            throw json::other_error::create(
                501, "Unknown RawContent type: " + type_str, &j);
        }
    }

private:
    Variant data_;
};

// Content = Annotated<RawContent>
using Content = Annotated<RawContent>;

// =============================================================================
// Content factory methods
// =============================================================================

namespace content_factories {

inline Content text(std::string text_value) {
    return Content::no_annotation(RawContent::text(std::move(text_value)));
}

inline Content image(std::string data, std::string mime_type) {
    return Content::no_annotation(RawContent::image(std::move(data), std::move(mime_type)));
}

inline Content resource(ResourceContents res) {
    return Content::no_annotation(RawContent::resource(std::move(res)));
}

inline Content audio(std::string data, std::string mime_type) {
    return Content::no_annotation(RawContent::audio(std::move(data), std::move(mime_type)));
}

inline Content resource_link(RawResource raw_resource) {
    return Content::no_annotation(RawContent::resource_link(std::move(raw_resource)));
}

inline Content embedded_text(std::string uri, std::string content_text) {
    return Content::no_annotation(
        RawContent::embedded_text(std::move(uri), std::move(content_text)));
}

} // namespace content_factories

// =============================================================================
// ToolUseContent
// =============================================================================

struct ToolUseContent {
    std::string id;
    std::string name;
    JsonObject input;
    std::optional<Meta> meta;

    ToolUseContent() = default;
    ToolUseContent(std::string id_, std::string name_, JsonObject input_)
        : id(std::move(id_)), name(std::move(name_)), input(std::move(input_)) {}

    bool operator==(const ToolUseContent& other) const {
        return id == other.id && name == other.name
            && input == other.input && meta == other.meta;
    }

    friend void to_json(json& j, const ToolUseContent& c) {
        j = json{{"id", c.id}, {"name", c.name}, {"input", c.input}};
        if (c.meta.has_value()) {
            j["_meta"] = *c.meta;
        }
    }

    friend void from_json(const json& j, ToolUseContent& c) {
        j.at("id").get_to(c.id);
        j.at("name").get_to(c.name);
        j.at("input").get_to(c.input);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            c.meta = j["_meta"].get<Meta>();
        }
    }
};

// =============================================================================
// ToolResultContent
// =============================================================================

struct ToolResultContent {
    std::optional<Meta> meta;
    std::string tool_use_id;
    std::vector<Content> content;  // defaults to empty
    std::optional<JsonObject> structured_content;
    std::optional<bool> is_error;

    ToolResultContent() = default;
    ToolResultContent(std::string tool_use_id_, std::vector<Content> content_)
        : tool_use_id(std::move(tool_use_id_)), content(std::move(content_)) {}

    static ToolResultContent error(std::string tool_use_id_, std::vector<Content> content_) {
        ToolResultContent r;
        r.tool_use_id = std::move(tool_use_id_);
        r.content = std::move(content_);
        r.is_error = true;
        return r;
    }

    bool operator==(const ToolResultContent& other) const {
        return meta == other.meta && tool_use_id == other.tool_use_id
            && content == other.content && structured_content == other.structured_content
            && is_error == other.is_error;
    }

    friend void to_json(json& j, const ToolResultContent& c) {
        j = json{{"toolUseId", c.tool_use_id}};
        if (!c.content.empty()) {
            j["content"] = c.content;
        }
        detail::set_opt(j, "structuredContent", c.structured_content);
        detail::set_opt(j, "isError", c.is_error);
        if (c.meta.has_value()) {
            j["_meta"] = *c.meta;
        }
    }

    friend void from_json(const json& j, ToolResultContent& c) {
        j.at("toolUseId").get_to(c.tool_use_id);
        if (j.contains("content") && !j["content"].is_null()) {
            j["content"].get_to(c.content);
        }
        detail::get_opt(j, "structuredContent", c.structured_content);
        detail::get_opt(j, "isError", c.is_error);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            c.meta = j["_meta"].get<Meta>();
        }
    }
};

// =============================================================================
// SamplingMessageContent
//
// Tagged variant with "type" field, snake_case:
//   Text | Image | Audio | ToolUse | ToolResult
// =============================================================================

class SamplingMessageContent {
public:
    using Variant = std::variant<
        RawTextContent,
        RawImageContent,
        RawAudioContent,
        ToolUseContent,
        ToolResultContent
    >;

    SamplingMessageContent() : data_(RawTextContent{}) {}
    SamplingMessageContent(RawTextContent v) : data_(std::move(v)) {}
    SamplingMessageContent(RawImageContent v) : data_(std::move(v)) {}
    SamplingMessageContent(RawAudioContent v) : data_(std::move(v)) {}
    SamplingMessageContent(ToolUseContent v) : data_(std::move(v)) {}
    SamplingMessageContent(ToolResultContent v) : data_(std::move(v)) {}

    bool is_text() const { return std::holds_alternative<RawTextContent>(data_); }
    bool is_image() const { return std::holds_alternative<RawImageContent>(data_); }
    bool is_audio() const { return std::holds_alternative<RawAudioContent>(data_); }
    bool is_tool_use() const { return std::holds_alternative<ToolUseContent>(data_); }
    bool is_tool_result() const { return std::holds_alternative<ToolResultContent>(data_); }

    const RawTextContent* as_text() const { return std::get_if<RawTextContent>(&data_); }
    const RawImageContent* as_image() const { return std::get_if<RawImageContent>(&data_); }
    const RawAudioContent* as_audio() const { return std::get_if<RawAudioContent>(&data_); }
    const ToolUseContent* as_tool_use() const { return std::get_if<ToolUseContent>(&data_); }
    const ToolResultContent* as_tool_result() const {
        return std::get_if<ToolResultContent>(&data_);
    }

    static SamplingMessageContent text(std::string text_value) {
        return SamplingMessageContent(RawTextContent(std::move(text_value)));
    }

    static SamplingMessageContent tool_use(
        std::string id, std::string name, JsonObject input
    ) {
        return SamplingMessageContent(
            ToolUseContent(std::move(id), std::move(name), std::move(input)));
    }

    bool operator==(const SamplingMessageContent& other) const {
        return data_ == other.data_;
    }

    friend void to_json(json& j, const SamplingMessageContent& c) {
        static const char* tags[] = {"text", "image", "audio", "tool_use", "tool_result"};
        std::visit([&j](const auto& v) { j = v; }, c.data_);
        j["type"] = tags[c.data_.index()];
    }

    friend void from_json(const json& j, SamplingMessageContent& c) {
        auto type_str = j.at("type").get<std::string>();
        if (type_str == "text") {
            RawTextContent v; from_json(j, v); c.data_ = std::move(v);
        } else if (type_str == "image") {
            RawImageContent v; from_json(j, v); c.data_ = std::move(v);
        } else if (type_str == "audio") {
            RawAudioContent v; from_json(j, v); c.data_ = std::move(v);
        } else if (type_str == "tool_use") {
            ToolUseContent v; from_json(j, v); c.data_ = std::move(v);
        } else if (type_str == "tool_result") {
            ToolResultContent v; from_json(j, v); c.data_ = std::move(v);
        } else {
            throw json::other_error::create(
                501, "Unknown SamplingMessageContent type: " + type_str, &j);
        }
    }

private:
    Variant data_;
};

// =============================================================================
// AssistantMessageContent
//
// Tagged variant: Text | Image | Audio | ToolUse
// =============================================================================

class AssistantMessageContent {
public:
    using Variant = std::variant<
        RawTextContent,
        RawImageContent,
        RawAudioContent,
        ToolUseContent
    >;

    AssistantMessageContent() : data_(RawTextContent{}) {}
    AssistantMessageContent(RawTextContent v) : data_(std::move(v)) {}
    AssistantMessageContent(RawImageContent v) : data_(std::move(v)) {}
    AssistantMessageContent(RawAudioContent v) : data_(std::move(v)) {}
    AssistantMessageContent(ToolUseContent v) : data_(std::move(v)) {}

    bool is_text() const { return std::holds_alternative<RawTextContent>(data_); }
    bool is_image() const { return std::holds_alternative<RawImageContent>(data_); }
    bool is_audio() const { return std::holds_alternative<RawAudioContent>(data_); }
    bool is_tool_use() const { return std::holds_alternative<ToolUseContent>(data_); }

    const RawTextContent* as_text() const { return std::get_if<RawTextContent>(&data_); }
    const RawImageContent* as_image() const { return std::get_if<RawImageContent>(&data_); }
    const RawAudioContent* as_audio() const { return std::get_if<RawAudioContent>(&data_); }
    const ToolUseContent* as_tool_use() const { return std::get_if<ToolUseContent>(&data_); }

    static AssistantMessageContent text(std::string text_value) {
        return AssistantMessageContent(RawTextContent(std::move(text_value)));
    }

    static AssistantMessageContent tool_use(
        std::string id, std::string name, JsonObject input
    ) {
        return AssistantMessageContent(
            ToolUseContent(std::move(id), std::move(name), std::move(input)));
    }

    bool operator==(const AssistantMessageContent& other) const {
        return data_ == other.data_;
    }

    friend void to_json(json& j, const AssistantMessageContent& c) {
        static const char* tags[] = {"text", "image", "audio", "tool_use"};
        std::visit([&j](const auto& v) { j = v; }, c.data_);
        j["type"] = tags[c.data_.index()];
    }

    friend void from_json(const json& j, AssistantMessageContent& c) {
        auto type_str = j.at("type").get<std::string>();
        if (type_str == "text") {
            RawTextContent v; from_json(j, v); c.data_ = std::move(v);
        } else if (type_str == "image") {
            RawImageContent v; from_json(j, v); c.data_ = std::move(v);
        } else if (type_str == "audio") {
            RawAudioContent v; from_json(j, v); c.data_ = std::move(v);
        } else if (type_str == "tool_use") {
            ToolUseContent v; from_json(j, v); c.data_ = std::move(v);
        } else {
            throw json::other_error::create(
                501, "Unknown AssistantMessageContent type: " + type_str, &j);
        }
    }

private:
    Variant data_;
};

// =============================================================================
// UserMessageContent
//
// Tagged variant: Text | Image | Audio | ToolResult
// =============================================================================

class UserMessageContent {
public:
    using Variant = std::variant<
        RawTextContent,
        RawImageContent,
        RawAudioContent,
        ToolResultContent
    >;

    UserMessageContent() : data_(RawTextContent{}) {}
    UserMessageContent(RawTextContent v) : data_(std::move(v)) {}
    UserMessageContent(RawImageContent v) : data_(std::move(v)) {}
    UserMessageContent(RawAudioContent v) : data_(std::move(v)) {}
    UserMessageContent(ToolResultContent v) : data_(std::move(v)) {}

    bool is_text() const { return std::holds_alternative<RawTextContent>(data_); }
    bool is_image() const { return std::holds_alternative<RawImageContent>(data_); }
    bool is_audio() const { return std::holds_alternative<RawAudioContent>(data_); }
    bool is_tool_result() const { return std::holds_alternative<ToolResultContent>(data_); }

    const RawTextContent* as_text() const { return std::get_if<RawTextContent>(&data_); }
    const RawImageContent* as_image() const { return std::get_if<RawImageContent>(&data_); }
    const RawAudioContent* as_audio() const { return std::get_if<RawAudioContent>(&data_); }
    const ToolResultContent* as_tool_result() const {
        return std::get_if<ToolResultContent>(&data_);
    }

    static UserMessageContent text(std::string text_value) {
        return UserMessageContent(RawTextContent(std::move(text_value)));
    }

    static UserMessageContent tool_result(
        std::string tool_use_id, std::vector<Content> content
    ) {
        return UserMessageContent(
            ToolResultContent(std::move(tool_use_id), std::move(content)));
    }

    static UserMessageContent tool_result_error(
        std::string tool_use_id, std::vector<Content> content
    ) {
        return UserMessageContent(
            ToolResultContent::error(std::move(tool_use_id), std::move(content)));
    }

    bool operator==(const UserMessageContent& other) const {
        return data_ == other.data_;
    }

    friend void to_json(json& j, const UserMessageContent& c) {
        static const char* tags[] = {"text", "image", "audio", "tool_result"};
        std::visit([&j](const auto& v) { j = v; }, c.data_);
        j["type"] = tags[c.data_.index()];
    }

    friend void from_json(const json& j, UserMessageContent& c) {
        auto type_str = j.at("type").get<std::string>();
        if (type_str == "text") {
            RawTextContent v; from_json(j, v); c.data_ = std::move(v);
        } else if (type_str == "image") {
            RawImageContent v; from_json(j, v); c.data_ = std::move(v);
        } else if (type_str == "audio") {
            RawAudioContent v; from_json(j, v); c.data_ = std::move(v);
        } else if (type_str == "tool_result") {
            ToolResultContent v; from_json(j, v); c.data_ = std::move(v);
        } else {
            throw json::other_error::create(
                501, "Unknown UserMessageContent type: " + type_str, &j);
        }
    }

private:
    Variant data_;
};

} // namespace mcp
