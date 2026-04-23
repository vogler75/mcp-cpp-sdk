#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace mcp {

using json = nlohmann::json;
using JsonObject = nlohmann::json::object_t;

// =============================================================================
// Protocol Version
// =============================================================================

class ProtocolVersion {
public:
    static const ProtocolVersion V_2025_11_25;
    static const ProtocolVersion V_2025_06_18;
    static const ProtocolVersion V_2025_03_26;
    static const ProtocolVersion V_2024_11_05;
    static const ProtocolVersion LATEST;

    static const std::vector<ProtocolVersion>& known_versions();

    ProtocolVersion() : value_("2025-11-25") {}
    explicit ProtocolVersion(std::string value) : value_(std::move(value)) {}

    const std::string& value() const { return value_; }

    bool operator==(const ProtocolVersion& other) const { return value_ == other.value_; }
    bool operator!=(const ProtocolVersion& other) const { return value_ != other.value_; }
    bool operator<(const ProtocolVersion& other) const { return value_ < other.value_; }

    friend void to_json(json& j, const ProtocolVersion& v) { j = v.value_; }
    friend void from_json(const json& j, ProtocolVersion& v) { v.value_ = j.get<std::string>(); }

private:
    std::string value_;
};

// =============================================================================
// NumberOrString
// =============================================================================

class NumberOrString {
public:
    NumberOrString() : data_(static_cast<int64_t>(0)) {}
    explicit NumberOrString(int64_t n) : data_(n) {}
    explicit NumberOrString(std::string s) : data_(std::move(s)) {}

    bool is_number() const { return std::holds_alternative<int64_t>(data_); }
    bool is_string() const { return std::holds_alternative<std::string>(data_); }

    int64_t as_number() const { return std::get<int64_t>(data_); }
    const std::string& as_string() const { return std::get<std::string>(data_); }

    json to_json_value() const {
        if (is_number()) return json(as_number());
        return json(as_string());
    }

    std::string to_string() const {
        if (is_number()) return std::to_string(as_number());
        return as_string();
    }

    bool operator==(const NumberOrString& other) const { return data_ == other.data_; }
    bool operator!=(const NumberOrString& other) const { return data_ != other.data_; }

    friend void to_json(json& j, const NumberOrString& v) {
        if (v.is_number()) {
            j = v.as_number();
        } else {
            j = v.as_string();
        }
    }

    friend void from_json(const json& j, NumberOrString& v) {
        if (j.is_number_integer()) {
            v.data_ = j.get<int64_t>();
        } else if (j.is_string()) {
            v.data_ = j.get<std::string>();
        } else {
            throw json::type_error::create(302, "Expected number or string", &j);
        }
    }

private:
    std::variant<int64_t, std::string> data_;
};

} // namespace mcp

namespace std {
template <>
struct hash<mcp::NumberOrString> {
    size_t operator()(const mcp::NumberOrString& v) const {
        if (v.is_number()) return hash<int64_t>{}(v.as_number());
        return hash<string>{}(v.as_string());
    }
};
} // namespace std

namespace mcp {

// Type aliases
using RequestId = NumberOrString;
using Cursor = std::string;

// =============================================================================
// Helper: optional JSON field serialization
// =============================================================================

namespace detail {

template <typename T>
void set_opt(json& j, const char* key, const std::optional<T>& opt) {
    if (opt.has_value()) {
        j[key] = *opt;
    }
}

template <typename T>
void get_opt(const json& j, const char* key, std::optional<T>& opt) {
    if (j.contains(key) && !j[key].is_null()) {
        opt = j[key].get<T>();
    }
}

} // namespace detail

// =============================================================================
// ProgressToken
// =============================================================================

struct ProgressToken {
    NumberOrString value;

    ProgressToken() = default;
    explicit ProgressToken(NumberOrString v) : value(std::move(v)) {}

    bool operator==(const ProgressToken& other) const { return value == other.value; }
    bool operator!=(const ProgressToken& other) const { return value != other.value; }

    friend void to_json(json& j, const ProgressToken& t) { j = t.value; }
    friend void from_json(const json& j, ProgressToken& t) { t.value = j.get<NumberOrString>(); }
};

// =============================================================================
// EmptyObject
// =============================================================================

struct EmptyObject {
    friend void to_json(json& j, const EmptyObject&) { j = json::object(); }
    friend void from_json(const json&, EmptyObject&) {}

    bool operator==(const EmptyObject&) const { return true; }
};

using EmptyResult = EmptyObject;

// =============================================================================
// ErrorCode
// =============================================================================

struct ErrorCode {
    int32_t code;

    static const ErrorCode RESOURCE_NOT_FOUND;
    static const ErrorCode INVALID_REQUEST;
    static const ErrorCode METHOD_NOT_FOUND;
    static const ErrorCode INVALID_PARAMS;
    static const ErrorCode INTERNAL_ERROR;
    static const ErrorCode PARSE_ERROR;
    static const ErrorCode REQUEST_TIMEOUT;

    ErrorCode() : code(0) {}
    explicit ErrorCode(int32_t c) : code(c) {}

    bool operator==(const ErrorCode& other) const { return code == other.code; }
    bool operator!=(const ErrorCode& other) const { return code != other.code; }

    friend void to_json(json& j, const ErrorCode& e) { j = e.code; }
    friend void from_json(const json& j, ErrorCode& e) { e.code = j.get<int32_t>(); }
};

// =============================================================================
// ErrorData
// =============================================================================

struct ErrorData {
    ErrorCode code;
    std::string message;
    std::optional<json> data;

    ErrorData() = default;
    ErrorData(ErrorCode c, std::string msg, std::optional<json> d = std::nullopt)
        : code(c), message(std::move(msg)), data(std::move(d)) {}

    static ErrorData resource_not_found(std::string msg, std::optional<json> data = std::nullopt) {
        return {ErrorCode::RESOURCE_NOT_FOUND, std::move(msg), std::move(data)};
    }
    static ErrorData parse_error(std::string msg, std::optional<json> data = std::nullopt) {
        return {ErrorCode::PARSE_ERROR, std::move(msg), std::move(data)};
    }
    static ErrorData invalid_request(std::string msg, std::optional<json> data = std::nullopt) {
        return {ErrorCode::INVALID_REQUEST, std::move(msg), std::move(data)};
    }
    static ErrorData method_not_found(const std::string& method) {
        return {ErrorCode::METHOD_NOT_FOUND, method, std::nullopt};
    }
    static ErrorData invalid_params(std::string msg, std::optional<json> data = std::nullopt) {
        return {ErrorCode::INVALID_PARAMS, std::move(msg), std::move(data)};
    }
    static ErrorData internal_error(std::string msg, std::optional<json> data = std::nullopt) {
        return {ErrorCode::INTERNAL_ERROR, std::move(msg), std::move(data)};
    }

    bool operator==(const ErrorData& other) const {
        return code == other.code && message == other.message && data == other.data;
    }

    friend void to_json(json& j, const ErrorData& e) {
        j = json{{"code", e.code}, {"message", e.message}};
        if (e.data.has_value()) {
            j["data"] = *e.data;
        }
    }
    friend void from_json(const json& j, ErrorData& e) {
        j.at("code").get_to(e.code);
        j.at("message").get_to(e.message);
        if (j.contains("data") && !j["data"].is_null()) {
            e.data = j["data"];
        }
    }
};

// =============================================================================
// CustomResult
// =============================================================================

struct CustomResult {
    json value;

    CustomResult() = default;
    explicit CustomResult(json v) : value(std::move(v)) {}

    template <typename T>
    T result_as() const {
        return value.get<T>();
    }

    bool operator==(const CustomResult& other) const { return value == other.value; }

    friend void to_json(json& j, const CustomResult& r) { j = r.value; }
    friend void from_json(const json& j, CustomResult& r) { r.value = j; }
};

// =============================================================================
// Implementation (server/client info)
// =============================================================================

enum class IconTheme {
    Light,
    Dark,
};

inline void to_json(json& j, const IconTheme& t) {
    switch (t) {
        case IconTheme::Light: j = "light"; break;
        case IconTheme::Dark:  j = "dark";  break;
    }
}
inline void from_json(const json& j, IconTheme& t) {
    const auto s = j.get<std::string>();
    if (s == "light") t = IconTheme::Light;
    else if (s == "dark") t = IconTheme::Dark;
    else throw std::runtime_error("unknown IconTheme: " + s);
}

struct Icon {
    std::string src;
    std::optional<std::string> mime_type;
    std::optional<std::vector<std::string>> sizes;
    std::optional<IconTheme> theme;

    bool operator==(const Icon& other) const {
        return src == other.src && mime_type == other.mime_type
            && sizes == other.sizes && theme == other.theme;
    }

    friend void to_json(json& j, const Icon& i) {
        j = json{{"src", i.src}};
        if (i.mime_type) j["mimeType"] = *i.mime_type;
        if (i.sizes) j["sizes"] = *i.sizes;
        if (i.theme) j["theme"] = *i.theme;
    }
    friend void from_json(const json& j, Icon& i) {
        j.at("src").get_to(i.src);
        detail::get_opt(j, "mimeType", i.mime_type);
        detail::get_opt(j, "sizes", i.sizes);
        detail::get_opt(j, "theme", i.theme);
    }
};

struct Implementation {
    std::string name;
    std::optional<std::string> title;
    std::string version;
    std::optional<std::vector<Icon>> icons;
    std::optional<std::string> website_url;

    Implementation() : name("mcp-cpp"), version("0.1.0") {}
    Implementation(std::string n, std::string v)
        : name(std::move(n)), version(std::move(v)) {}

    static Implementation from_build_env() {
        return Implementation("mcp-cpp", "0.1.0");
    }

    bool operator==(const Implementation& other) const {
        return name == other.name && title == other.title && version == other.version
            && icons == other.icons && website_url == other.website_url;
    }

    friend void to_json(json& j, const Implementation& i) {
        j = json{{"name", i.name}, {"version", i.version}};
        if (i.title) j["title"] = *i.title;
        if (i.icons) j["icons"] = *i.icons;
        if (i.website_url) j["websiteUrl"] = *i.website_url;
    }
    friend void from_json(const json& j, Implementation& i) {
        j.at("name").get_to(i.name);
        j.at("version").get_to(i.version);
        detail::get_opt(j, "title", i.title);
        detail::get_opt(j, "icons", i.icons);
        detail::get_opt(j, "websiteUrl", i.website_url);
    }
};

} // namespace mcp
