#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "mcp/model/types.hpp"
#include "mcp/model/meta.hpp"

namespace mcp {

// =============================================================================
// CompletionContext
// =============================================================================

struct CompletionContext {
    std::optional<std::map<std::string, std::string>> arguments;

    bool operator==(const CompletionContext& other) const {
        return arguments == other.arguments;
    }

    friend void to_json(json& j, const CompletionContext& c) {
        j = json::object();
        if (c.arguments.has_value()) {
            j["arguments"] = *c.arguments;
        }
    }

    friend void from_json(const json& j, CompletionContext& c) {
        if (j.contains("arguments") && !j["arguments"].is_null()) {
            c.arguments = j["arguments"].get<std::map<std::string, std::string>>();
        }
    }
};

// =============================================================================
// Reference (tagged variant by "type" field)
// =============================================================================

struct ResourceReference {
    std::string uri;

    bool operator==(const ResourceReference& other) const { return uri == other.uri; }

    friend void to_json(json& j, const ResourceReference& r) {
        j = json{{"uri", r.uri}};
    }

    friend void from_json(const json& j, ResourceReference& r) {
        j.at("uri").get_to(r.uri);
    }
};

struct PromptReference {
    std::string name;
    std::optional<std::string> title;

    bool operator==(const PromptReference& other) const {
        return name == other.name && title == other.title;
    }

    friend void to_json(json& j, const PromptReference& r) {
        j = json{{"name", r.name}};
        detail::set_opt(j, "title", r.title);
    }

    friend void from_json(const json& j, PromptReference& r) {
        j.at("name").get_to(r.name);
        detail::get_opt(j, "title", r.title);
    }
};

/// Reference is a tagged variant discriminated by the "type" field.
/// "ref/resource" -> ResourceReference, "ref/prompt" -> PromptReference.
class Reference {
public:
    using Variant = std::variant<ResourceReference, PromptReference>;

    Reference() : data_(ResourceReference{}) {}
    Reference(ResourceReference r) : data_(std::move(r)) {}
    Reference(PromptReference p) : data_(std::move(p)) {}

    bool is_resource() const { return std::holds_alternative<ResourceReference>(data_); }
    bool is_prompt() const { return std::holds_alternative<PromptReference>(data_); }

    const ResourceReference& as_resource() const {
        return std::get<ResourceReference>(data_);
    }
    const PromptReference& as_prompt() const {
        return std::get<PromptReference>(data_);
    }

    /// Create a prompt reference.
    static Reference for_prompt(const std::string& name) {
        return Reference(PromptReference{name, std::nullopt});
    }

    /// Create a resource reference.
    static Reference for_resource(const std::string& uri) {
        return Reference(ResourceReference{uri});
    }

    bool operator==(const Reference& other) const { return data_ == other.data_; }

    friend void to_json(json& j, const Reference& r) {
        if (r.is_resource()) {
            j = r.as_resource();
            j["type"] = "ref/resource";
        } else {
            j = r.as_prompt();
            j["type"] = "ref/prompt";
        }
    }

    friend void from_json(const json& j, Reference& r) {
        auto type = j.at("type").get<std::string>();
        if (type == "ref/resource") {
            ResourceReference res;
            from_json(j, res);
            r.data_ = std::move(res);
        } else if (type == "ref/prompt") {
            PromptReference prom;
            from_json(j, prom);
            r.data_ = std::move(prom);
        } else {
            throw json::other_error::create(501, "Unknown Reference type: " + type, &j);
        }
    }

private:
    Variant data_;
};

// =============================================================================
// ArgumentInfo
// =============================================================================

struct ArgumentInfo {
    std::string name;
    std::string value;

    bool operator==(const ArgumentInfo& other) const {
        return name == other.name && value == other.value;
    }

    friend void to_json(json& j, const ArgumentInfo& a) {
        j = json{{"name", a.name}, {"value", a.value}};
    }

    friend void from_json(const json& j, ArgumentInfo& a) {
        j.at("name").get_to(a.name);
        j.at("value").get_to(a.value);
    }
};

// =============================================================================
// CompleteRequestParams
// =============================================================================

struct CompleteRequestParams {
    std::optional<Meta> meta;
    Reference ref_;
    ArgumentInfo argument;
    std::optional<CompletionContext> context;

    bool operator==(const CompleteRequestParams& other) const {
        return meta == other.meta && ref_ == other.ref_
            && argument == other.argument && context == other.context;
    }

    friend void to_json(json& j, const CompleteRequestParams& p) {
        j = json{{"ref", p.ref_}, {"argument", p.argument}};
        if (p.meta.has_value()) {
            j["_meta"] = *p.meta;
        }
        detail::set_opt(j, "context", p.context);
    }

    friend void from_json(const json& j, CompleteRequestParams& p) {
        j.at("ref").get_to(p.ref_);
        j.at("argument").get_to(p.argument);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            p.meta = j["_meta"].get<Meta>();
        }
        detail::get_opt(j, "context", p.context);
    }
};

// =============================================================================
// CompletionInfo
// =============================================================================

struct CompletionInfo {
    std::vector<std::string> values;
    std::optional<uint32_t> total;
    std::optional<bool> has_more;

    static constexpr size_t MAX_VALUES = 100;

    bool operator==(const CompletionInfo& other) const {
        return values == other.values && total == other.total && has_more == other.has_more;
    }

    friend void to_json(json& j, const CompletionInfo& c) {
        j = json{{"values", c.values}};
        detail::set_opt(j, "total", c.total);
        detail::set_opt(j, "hasMore", c.has_more);
    }

    friend void from_json(const json& j, CompletionInfo& c) {
        j.at("values").get_to(c.values);
        detail::get_opt(j, "total", c.total);
        detail::get_opt(j, "hasMore", c.has_more);
    }
};

// =============================================================================
// CompleteResult
// =============================================================================

struct CompleteResult {
    CompletionInfo completion;

    bool operator==(const CompleteResult& other) const {
        return completion == other.completion;
    }

    friend void to_json(json& j, const CompleteResult& r) {
        j = json{{"completion", r.completion}};
    }

    friend void from_json(const json& j, CompleteResult& r) {
        j.at("completion").get_to(r.completion);
    }
};

} // namespace mcp
