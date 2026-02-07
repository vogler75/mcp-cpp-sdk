#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "mcp/model/meta.hpp"
#include "mcp/model/types.hpp"

namespace mcp {

// =============================================================================
// Role
// =============================================================================

enum class Role {
    User,
    Assistant,
};

inline void to_json(json& j, const Role& r) {
    switch (r) {
        case Role::User: j = "user"; break;
        case Role::Assistant: j = "assistant"; break;
    }
}

inline void from_json(const json& j, Role& r) {
    auto s = j.get<std::string>();
    if (s == "user") {
        r = Role::User;
    } else if (s == "assistant") {
        r = Role::Assistant;
    } else {
        throw json::other_error::create(501, "Unknown Role value: " + s, &j);
    }
}

// =============================================================================
// Annotations
// =============================================================================

struct Annotations {
    std::optional<std::vector<Role>> audience;
    std::optional<float> priority;
    std::optional<std::string> last_modified;

    bool operator==(const Annotations& other) const {
        return audience == other.audience
            && priority == other.priority
            && last_modified == other.last_modified;
    }

    friend void to_json(json& j, const Annotations& a) {
        j = json::object();
        detail::set_opt(j, "audience", a.audience);
        detail::set_opt(j, "priority", a.priority);
        detail::set_opt(j, "lastModified", a.last_modified);
    }

    friend void from_json(const json& j, Annotations& a) {
        detail::get_opt(j, "audience", a.audience);
        detail::get_opt(j, "priority", a.priority);
        detail::get_opt(j, "lastModified", a.last_modified);
    }
};

// =============================================================================
// Annotated<T>
//
// Flattens T's fields at the top level alongside an optional "annotations"
// field. Serialization merges T's JSON with the annotations key.
// =============================================================================

template <typename T>
struct Annotated : T {
    std::optional<Annotations> annotations;

    Annotated() = default;

    Annotated(T raw, std::optional<Annotations> ann = std::nullopt)
        : T(std::move(raw)), annotations(std::move(ann)) {}

    /// Access the underlying raw value
    T& raw() { return static_cast<T&>(*this); }
    const T& raw() const { return static_cast<const T&>(*this); }

    /// Create an Annotated with no annotations
    static Annotated no_annotation(T raw) {
        return Annotated(std::move(raw));
    }

    friend void to_json(json& j, const Annotated& a) {
        // Serialize the base type first (flattened)
        j = static_cast<const T&>(a);
        // Add annotations alongside T's fields
        if (a.annotations.has_value()) {
            j["annotations"] = *a.annotations;
        }
    }

    friend void from_json(const json& j, Annotated& a) {
        // Deserialize base type from the same object (flattened)
        j.get_to(static_cast<T&>(a));
        // Read annotations from the same level
        detail::get_opt(j, "annotations", a.annotations);
    }

    bool operator==(const Annotated& other) const {
        return static_cast<const T&>(*this) == static_cast<const T&>(other)
            && annotations == other.annotations;
    }
};

// =============================================================================
// RawResource
// =============================================================================

struct RawResource {
    std::string uri;
    std::string name;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::string> mime_type;
    std::optional<uint32_t> size;
    std::optional<std::vector<Icon>> icons;
    std::optional<Meta> meta;

    RawResource() = default;

    RawResource(std::string uri_, std::string name_)
        : uri(std::move(uri_)), name(std::move(name_)) {}

    bool operator==(const RawResource& other) const {
        return uri == other.uri && name == other.name && title == other.title
            && description == other.description && mime_type == other.mime_type
            && size == other.size && icons == other.icons && meta == other.meta;
    }

    friend void to_json(json& j, const RawResource& r) {
        j = json{{"uri", r.uri}, {"name", r.name}};
        detail::set_opt(j, "title", r.title);
        detail::set_opt(j, "description", r.description);
        detail::set_opt(j, "mimeType", r.mime_type);
        detail::set_opt(j, "size", r.size);
        detail::set_opt(j, "icons", r.icons);
        if (r.meta.has_value()) {
            j["_meta"] = *r.meta;
        }
    }

    friend void from_json(const json& j, RawResource& r) {
        j.at("uri").get_to(r.uri);
        j.at("name").get_to(r.name);
        detail::get_opt(j, "title", r.title);
        detail::get_opt(j, "description", r.description);
        detail::get_opt(j, "mimeType", r.mime_type);
        detail::get_opt(j, "size", r.size);
        detail::get_opt(j, "icons", r.icons);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            r.meta = j["_meta"].get<Meta>();
        }
    }
};

using Resource = Annotated<RawResource>;

// =============================================================================
// RawResourceTemplate
// =============================================================================

struct RawResourceTemplate {
    std::string uri_template;
    std::string name;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::string> mime_type;
    std::optional<std::vector<Icon>> icons;

    RawResourceTemplate() = default;

    RawResourceTemplate(std::string uri_template_, std::string name_)
        : uri_template(std::move(uri_template_)), name(std::move(name_)) {}

    bool operator==(const RawResourceTemplate& other) const {
        return uri_template == other.uri_template && name == other.name
            && title == other.title && description == other.description
            && mime_type == other.mime_type && icons == other.icons;
    }

    friend void to_json(json& j, const RawResourceTemplate& r) {
        j = json{{"uriTemplate", r.uri_template}, {"name", r.name}};
        detail::set_opt(j, "title", r.title);
        detail::set_opt(j, "description", r.description);
        detail::set_opt(j, "mimeType", r.mime_type);
        detail::set_opt(j, "icons", r.icons);
    }

    friend void from_json(const json& j, RawResourceTemplate& r) {
        j.at("uriTemplate").get_to(r.uri_template);
        j.at("name").get_to(r.name);
        detail::get_opt(j, "title", r.title);
        detail::get_opt(j, "description", r.description);
        detail::get_opt(j, "mimeType", r.mime_type);
        detail::get_opt(j, "icons", r.icons);
    }
};

using ResourceTemplate = Annotated<RawResourceTemplate>;

// =============================================================================
// ResourceContents (untagged variant: TextResourceContents | BlobResourceContents)
// =============================================================================

struct TextResourceContents {
    std::string uri;
    std::optional<std::string> mime_type;
    std::string text;
    std::optional<Meta> meta;

    bool operator==(const TextResourceContents& other) const {
        return uri == other.uri && mime_type == other.mime_type
            && text == other.text && meta == other.meta;
    }

    friend void to_json(json& j, const TextResourceContents& t) {
        j = json{{"uri", t.uri}, {"text", t.text}};
        detail::set_opt(j, "mimeType", t.mime_type);
        if (t.meta.has_value()) {
            j["_meta"] = *t.meta;
        }
    }

    friend void from_json(const json& j, TextResourceContents& t) {
        j.at("uri").get_to(t.uri);
        j.at("text").get_to(t.text);
        detail::get_opt(j, "mimeType", t.mime_type);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            t.meta = j["_meta"].get<Meta>();
        }
    }
};

struct BlobResourceContents {
    std::string uri;
    std::optional<std::string> mime_type;
    std::string blob;
    std::optional<Meta> meta;

    bool operator==(const BlobResourceContents& other) const {
        return uri == other.uri && mime_type == other.mime_type
            && blob == other.blob && meta == other.meta;
    }

    friend void to_json(json& j, const BlobResourceContents& b) {
        j = json{{"uri", b.uri}, {"blob", b.blob}};
        detail::set_opt(j, "mimeType", b.mime_type);
        if (b.meta.has_value()) {
            j["_meta"] = *b.meta;
        }
    }

    friend void from_json(const json& j, BlobResourceContents& b) {
        j.at("uri").get_to(b.uri);
        j.at("blob").get_to(b.blob);
        detail::get_opt(j, "mimeType", b.mime_type);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            b.meta = j["_meta"].get<Meta>();
        }
    }
};

/// ResourceContents is an untagged variant: deserialization tries
/// TextResourceContents first (has "text" field), then BlobResourceContents
/// (has "blob" field).
class ResourceContents {
public:
    using Variant = std::variant<TextResourceContents, BlobResourceContents>;

    ResourceContents() : data_(TextResourceContents{}) {}
    ResourceContents(TextResourceContents t) : data_(std::move(t)) {}
    ResourceContents(BlobResourceContents b) : data_(std::move(b)) {}

    bool is_text() const { return std::holds_alternative<TextResourceContents>(data_); }
    bool is_blob() const { return std::holds_alternative<BlobResourceContents>(data_); }

    const TextResourceContents& as_text() const {
        return std::get<TextResourceContents>(data_);
    }
    TextResourceContents& as_text() {
        return std::get<TextResourceContents>(data_);
    }

    const BlobResourceContents& as_blob() const {
        return std::get<BlobResourceContents>(data_);
    }
    BlobResourceContents& as_blob() {
        return std::get<BlobResourceContents>(data_);
    }

    /// Factory: create a text resource
    static ResourceContents text(
        std::string text_value,
        std::string uri,
        std::optional<std::string> mime_type = std::string("text")
    ) {
        TextResourceContents t;
        t.uri = std::move(uri);
        t.mime_type = std::move(mime_type);
        t.text = std::move(text_value);
        return ResourceContents(std::move(t));
    }

    bool operator==(const ResourceContents& other) const { return data_ == other.data_; }

    friend void to_json(json& j, const ResourceContents& rc) {
        std::visit([&j](const auto& v) { j = v; }, rc.data_);
    }

    friend void from_json(const json& j, ResourceContents& rc) {
        // Untagged: try text first (has "text" key), then blob (has "blob" key)
        if (j.contains("text")) {
            TextResourceContents t;
            from_json(j, t);
            rc.data_ = std::move(t);
        } else if (j.contains("blob")) {
            BlobResourceContents b;
            from_json(j, b);
            rc.data_ = std::move(b);
        } else {
            throw json::other_error::create(
                501, "ResourceContents must contain 'text' or 'blob' field", &j);
        }
    }

private:
    Variant data_;
};

} // namespace mcp
