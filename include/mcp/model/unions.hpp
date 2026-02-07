#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "mcp/model/types.hpp"
#include "mcp/model/meta.hpp"
#include "mcp/model/resource.hpp"
#include "mcp/model/content.hpp"
#include "mcp/model/tool.hpp"
#include "mcp/model/prompt.hpp"
#include "mcp/model/capabilities.hpp"
#include "mcp/model/sampling.hpp"
#include "mcp/model/elicitation.hpp"
#include "mcp/model/task.hpp"
#include "mcp/model/logging.hpp"
#include "mcp/model/completion.hpp"
#include "mcp/model/roots.hpp"
#include "mcp/model/pagination.hpp"
#include "mcp/model/notifications.hpp"
#include "mcp/model/init.hpp"
#include "mcp/model/jsonrpc.hpp"

namespace mcp {

// =============================================================================
// Additional param/result types needed for unions
// =============================================================================

struct ReadResourceRequestParams {
    std::optional<Meta> meta;
    std::string uri;

    bool operator==(const ReadResourceRequestParams& other) const {
        return meta == other.meta && uri == other.uri;
    }

    friend void to_json(json& j, const ReadResourceRequestParams& p) {
        j = json{{"uri", p.uri}};
        if (p.meta.has_value()) {
            j["_meta"] = *p.meta;
        }
    }

    friend void from_json(const json& j, ReadResourceRequestParams& p) {
        j.at("uri").get_to(p.uri);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            p.meta = j["_meta"].get<Meta>();
        }
    }
};

struct ReadResourceResult {
    std::vector<ResourceContents> contents;

    bool operator==(const ReadResourceResult& other) const {
        return contents == other.contents;
    }

    friend void to_json(json& j, const ReadResourceResult& r) {
        j = json{{"contents", r.contents}};
    }

    friend void from_json(const json& j, ReadResourceResult& r) {
        j.at("contents").get_to(r.contents);
    }
};

struct SubscribeRequestParams {
    std::optional<Meta> meta;
    std::string uri;

    bool operator==(const SubscribeRequestParams& other) const {
        return meta == other.meta && uri == other.uri;
    }

    friend void to_json(json& j, const SubscribeRequestParams& p) {
        j = json{{"uri", p.uri}};
        if (p.meta.has_value()) {
            j["_meta"] = *p.meta;
        }
    }

    friend void from_json(const json& j, SubscribeRequestParams& p) {
        j.at("uri").get_to(p.uri);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            p.meta = j["_meta"].get<Meta>();
        }
    }
};

struct UnsubscribeRequestParams {
    std::optional<Meta> meta;
    std::string uri;

    bool operator==(const UnsubscribeRequestParams& other) const {
        return meta == other.meta && uri == other.uri;
    }

    friend void to_json(json& j, const UnsubscribeRequestParams& p) {
        j = json{{"uri", p.uri}};
        if (p.meta.has_value()) {
            j["_meta"] = *p.meta;
        }
    }

    friend void from_json(const json& j, UnsubscribeRequestParams& p) {
        j.at("uri").get_to(p.uri);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            p.meta = j["_meta"].get<Meta>();
        }
    }
};

// GetPromptRequestParams, GetPromptResult, ListPromptsResult -> prompt.hpp
// CallToolRequestParams, CallToolResult -> tool.hpp
// CreateMessageRequestParams, CreateMessageResult -> sampling.hpp

// =============================================================================
// Individual Request/Notification type wrappers
//
// Each wraps a method string constant and its params type.
// Serialized as { "method": "...", "params": {...} }.
// =============================================================================

// --- Helper macro for request type definitions ---
#define MCP_DEFINE_REQUEST(Name, MethodStr, ParamsType, HasParams)                  \
    struct Name {                                                                    \
        static constexpr const char* method_str = MethodStr;                         \
        MCP_REQUEST_PARAMS_FIELD_##HasParams(ParamsType)                              \
        std::string method() const { return method_str; }                            \
        bool operator==(const Name& other) const {                                   \
            return MCP_REQUEST_EQ_##HasParams(other);                                 \
        }                                                                            \
        friend void to_json(json& j, const Name& r) {                               \
            j = json{{"method", std::string(r.method_str)}};                         \
            MCP_REQUEST_TO_JSON_##HasParams(j, r)                                     \
        }                                                                            \
        friend void from_json(const json& j, Name& r) {                             \
            MCP_REQUEST_FROM_JSON_##HasParams(j, r)                                   \
        }                                                                            \
    };

// Params field helpers
#define MCP_REQUEST_PARAMS_FIELD_REQUIRED(T)   T params;
#define MCP_REQUEST_PARAMS_FIELD_OPTIONAL(T)   std::optional<T> params;
#define MCP_REQUEST_PARAMS_FIELD_NONE(T)

// Equality helpers
#define MCP_REQUEST_EQ_REQUIRED(other)  params == other.params
#define MCP_REQUEST_EQ_OPTIONAL(other)  params == other.params
#define MCP_REQUEST_EQ_NONE(other)      true

// to_json helpers
#define MCP_REQUEST_TO_JSON_REQUIRED(j, r)  j["params"] = r.params;
#define MCP_REQUEST_TO_JSON_OPTIONAL(j, r)  if (r.params.has_value()) { j["params"] = *r.params; }
#define MCP_REQUEST_TO_JSON_NONE(j, r)

// from_json helpers
#define MCP_REQUEST_FROM_JSON_REQUIRED(j, r)  j.at("params").get_to(r.params);
#define MCP_REQUEST_FROM_JSON_OPTIONAL(j, r)  \
    if (j.contains("params") && !j["params"].is_null()) { \
        r.params = j["params"].get<std::remove_reference_t< \
            decltype(*r.params)>>(); \
    }
#define MCP_REQUEST_FROM_JSON_NONE(j, r)

// =============================================================================
// Client Requests
// =============================================================================

MCP_DEFINE_REQUEST(PingRequest, "ping", void, NONE)
MCP_DEFINE_REQUEST(InitializeRequest, "initialize", InitializeRequestParams, REQUIRED)
MCP_DEFINE_REQUEST(CompleteRequest, "completion/complete", CompleteRequestParams, REQUIRED)
MCP_DEFINE_REQUEST(SetLevelRequest, "logging/setLevel", SetLevelRequestParams, REQUIRED)
MCP_DEFINE_REQUEST(GetPromptRequest, "prompts/get", GetPromptRequestParams, REQUIRED)
MCP_DEFINE_REQUEST(ListPromptsRequest, "prompts/list", PaginatedRequestParams, OPTIONAL)
MCP_DEFINE_REQUEST(ListResourcesRequest, "resources/list", PaginatedRequestParams, OPTIONAL)
MCP_DEFINE_REQUEST(
    ListResourceTemplatesRequest,
    "resources/templates/list",
    PaginatedRequestParams,
    OPTIONAL
)
MCP_DEFINE_REQUEST(ReadResourceRequest, "resources/read", ReadResourceRequestParams, REQUIRED)
MCP_DEFINE_REQUEST(SubscribeRequest, "resources/subscribe", SubscribeRequestParams, REQUIRED)
MCP_DEFINE_REQUEST(
    UnsubscribeRequest,
    "resources/unsubscribe",
    UnsubscribeRequestParams,
    REQUIRED
)
MCP_DEFINE_REQUEST(CallToolRequest, "tools/call", CallToolRequestParams, REQUIRED)
MCP_DEFINE_REQUEST(ListToolsRequest, "tools/list", PaginatedRequestParams, OPTIONAL)
MCP_DEFINE_REQUEST(GetTaskInfoRequest, "tasks/get", GetTaskInfoParams, REQUIRED)
MCP_DEFINE_REQUEST(ListTasksRequest, "tasks/list", PaginatedRequestParams, OPTIONAL)
MCP_DEFINE_REQUEST(GetTaskResultRequest, "tasks/result", GetTaskResultParams, REQUIRED)
MCP_DEFINE_REQUEST(CancelTaskRequest, "tasks/cancel", CancelTaskParams, REQUIRED)

// Server-only requests
MCP_DEFINE_REQUEST(
    CreateMessageRequest,
    "sampling/createMessage",
    CreateMessageRequestParams,
    REQUIRED
)
MCP_DEFINE_REQUEST(ListRootsRequest, "roots/list", void, NONE)
MCP_DEFINE_REQUEST(
    CreateElicitationRequest,
    "elicitation/create",
    CreateElicitationRequestParams,
    REQUIRED
)

// =============================================================================
// Notification wrappers
// =============================================================================

struct CancelledNotification {
    static constexpr const char* method_str = "notifications/cancelled";
    CancelledNotificationParam params;

    std::string method() const { return method_str; }

    bool operator==(const CancelledNotification& other) const {
        return params == other.params;
    }

    friend void to_json(json& j, const CancelledNotification& n) {
        j = json{{"method", std::string(n.method_str)}, {"params", n.params}};
    }

    friend void from_json(const json& j, CancelledNotification& n) {
        j.at("params").get_to(n.params);
    }
};

struct ProgressNotification {
    static constexpr const char* method_str = "notifications/progress";
    ProgressNotificationParam params;

    std::string method() const { return method_str; }

    bool operator==(const ProgressNotification& other) const {
        return params == other.params;
    }

    friend void to_json(json& j, const ProgressNotification& n) {
        j = json{{"method", std::string(n.method_str)}, {"params", n.params}};
    }

    friend void from_json(const json& j, ProgressNotification& n) {
        j.at("params").get_to(n.params);
    }
};

struct InitializedNotification {
    static constexpr const char* method_str = "notifications/initialized";

    std::string method() const { return method_str; }

    bool operator==(const InitializedNotification&) const { return true; }

    friend void to_json(json& j, const InitializedNotification&) {
        j = json{{"method", std::string(InitializedNotification::method_str)}};
    }

    friend void from_json(const json&, InitializedNotification&) {}
};

struct RootsListChangedNotification {
    static constexpr const char* method_str = "notifications/roots/list_changed";

    std::string method() const { return method_str; }

    bool operator==(const RootsListChangedNotification&) const { return true; }

    friend void to_json(json& j, const RootsListChangedNotification&) {
        j = json{{"method", std::string(RootsListChangedNotification::method_str)}};
    }

    friend void from_json(const json&, RootsListChangedNotification&) {}
};

struct LoggingMessageNotification {
    static constexpr const char* method_str = "notifications/message";
    LoggingMessageNotificationParam params;

    std::string method() const { return method_str; }

    bool operator==(const LoggingMessageNotification& other) const {
        return params == other.params;
    }

    friend void to_json(json& j, const LoggingMessageNotification& n) {
        j = json{{"method", std::string(n.method_str)}, {"params", n.params}};
    }

    friend void from_json(const json& j, LoggingMessageNotification& n) {
        j.at("params").get_to(n.params);
    }
};

struct ResourceUpdatedNotification {
    static constexpr const char* method_str = "notifications/resources/updated";
    ResourceUpdatedNotificationParam params;

    std::string method() const { return method_str; }

    bool operator==(const ResourceUpdatedNotification& other) const {
        return params == other.params;
    }

    friend void to_json(json& j, const ResourceUpdatedNotification& n) {
        j = json{{"method", std::string(n.method_str)}, {"params", n.params}};
    }

    friend void from_json(const json& j, ResourceUpdatedNotification& n) {
        j.at("params").get_to(n.params);
    }
};

struct ResourceListChangedNotification {
    static constexpr const char* method_str = "notifications/resources/list_changed";

    std::string method() const { return method_str; }

    bool operator==(const ResourceListChangedNotification&) const { return true; }

    friend void to_json(json& j, const ResourceListChangedNotification&) {
        j = json{{"method", std::string(ResourceListChangedNotification::method_str)}};
    }

    friend void from_json(const json&, ResourceListChangedNotification&) {}
};

struct ToolListChangedNotification {
    static constexpr const char* method_str = "notifications/tools/list_changed";

    std::string method() const { return method_str; }

    bool operator==(const ToolListChangedNotification&) const { return true; }

    friend void to_json(json& j, const ToolListChangedNotification&) {
        j = json{{"method", std::string(ToolListChangedNotification::method_str)}};
    }

    friend void from_json(const json&, ToolListChangedNotification&) {}
};

struct PromptListChangedNotification {
    static constexpr const char* method_str = "notifications/prompts/list_changed";

    std::string method() const { return method_str; }

    bool operator==(const PromptListChangedNotification&) const { return true; }

    friend void to_json(json& j, const PromptListChangedNotification&) {
        j = json{{"method", std::string(PromptListChangedNotification::method_str)}};
    }

    friend void from_json(const json&, PromptListChangedNotification&) {}
};

// =============================================================================
// ClientRequest variant
// =============================================================================

class ClientRequestVariant {
public:
    using Variant = std::variant<
        PingRequest,
        InitializeRequest,
        CompleteRequest,
        SetLevelRequest,
        GetPromptRequest,
        ListPromptsRequest,
        ListResourcesRequest,
        ListResourceTemplatesRequest,
        ReadResourceRequest,
        SubscribeRequest,
        UnsubscribeRequest,
        CallToolRequest,
        ListToolsRequest,
        GetTaskInfoRequest,
        ListTasksRequest,
        GetTaskResultRequest,
        CancelTaskRequest,
        CustomRequest
    >;

    ClientRequestVariant() : data_(PingRequest{}) {}

    template <typename T>
    ClientRequestVariant(T val) : data_(std::move(val)) {}

    template <typename T>
    bool is() const { return std::holds_alternative<T>(data_); }

    template <typename T>
    const T& get() const { return std::get<T>(data_); }

    template <typename T>
    T& get() { return std::get<T>(data_); }

    std::string method() const {
        return std::visit([](const auto& v) -> std::string {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, CustomRequest>) {
                return v.method;
            } else {
                return v.method();
            }
        }, data_);
    }

    const Variant& variant() const { return data_; }
    Variant& variant() { return data_; }

    bool operator==(const ClientRequestVariant& other) const { return data_ == other.data_; }

    friend void to_json(json& j, const ClientRequestVariant& r) {
        std::visit([&j](const auto& v) { j = v; }, r.data_);
    }

    friend void from_json(const json& j, ClientRequestVariant& r) {
        if (!j.contains("method")) {
            throw json::other_error::create(
                501, "ClientRequest missing 'method' field", &j);
        }
        auto method = j.at("method").get<std::string>();

        if (method == PingRequest::method_str) {
            r.data_ = j.get<PingRequest>();
        } else if (method == InitializeRequest::method_str) {
            r.data_ = j.get<InitializeRequest>();
        } else if (method == CompleteRequest::method_str) {
            r.data_ = j.get<CompleteRequest>();
        } else if (method == SetLevelRequest::method_str) {
            r.data_ = j.get<SetLevelRequest>();
        } else if (method == GetPromptRequest::method_str) {
            r.data_ = j.get<GetPromptRequest>();
        } else if (method == ListPromptsRequest::method_str) {
            r.data_ = j.get<ListPromptsRequest>();
        } else if (method == ListResourcesRequest::method_str) {
            r.data_ = j.get<ListResourcesRequest>();
        } else if (method == ListResourceTemplatesRequest::method_str) {
            r.data_ = j.get<ListResourceTemplatesRequest>();
        } else if (method == ReadResourceRequest::method_str) {
            r.data_ = j.get<ReadResourceRequest>();
        } else if (method == SubscribeRequest::method_str) {
            r.data_ = j.get<SubscribeRequest>();
        } else if (method == UnsubscribeRequest::method_str) {
            r.data_ = j.get<UnsubscribeRequest>();
        } else if (method == CallToolRequest::method_str) {
            r.data_ = j.get<CallToolRequest>();
        } else if (method == ListToolsRequest::method_str) {
            r.data_ = j.get<ListToolsRequest>();
        } else if (method == GetTaskInfoRequest::method_str) {
            r.data_ = j.get<GetTaskInfoRequest>();
        } else if (method == ListTasksRequest::method_str) {
            r.data_ = j.get<ListTasksRequest>();
        } else if (method == GetTaskResultRequest::method_str) {
            r.data_ = j.get<GetTaskResultRequest>();
        } else if (method == CancelTaskRequest::method_str) {
            r.data_ = j.get<CancelTaskRequest>();
        } else {
            r.data_ = j.get<CustomRequest>();
        }
    }

private:
    Variant data_;
};

using ClientRequest = ClientRequestVariant;

// =============================================================================
// ClientNotification variant
// =============================================================================

class ClientNotificationVariant {
public:
    using Variant = std::variant<
        CancelledNotification,
        ProgressNotification,
        InitializedNotification,
        RootsListChangedNotification,
        CustomNotification
    >;

    ClientNotificationVariant() : data_(InitializedNotification{}) {}

    template <typename T>
    ClientNotificationVariant(T val) : data_(std::move(val)) {}

    template <typename T>
    bool is() const { return std::holds_alternative<T>(data_); }

    template <typename T>
    const T& get() const { return std::get<T>(data_); }

    std::string method() const {
        return std::visit([](const auto& v) -> std::string {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, CustomNotification>) {
                return v.method;
            } else {
                return v.method();
            }
        }, data_);
    }

    const Variant& variant() const { return data_; }

    bool operator==(const ClientNotificationVariant& other) const {
        return data_ == other.data_;
    }

    friend void to_json(json& j, const ClientNotificationVariant& n) {
        std::visit([&j](const auto& v) { j = v; }, n.data_);
    }

    friend void from_json(const json& j, ClientNotificationVariant& n) {
        if (!j.contains("method")) {
            throw json::other_error::create(
                501, "ClientNotification missing 'method' field", &j);
        }
        auto method = j.at("method").get<std::string>();

        if (method == CancelledNotification::method_str) {
            n.data_ = j.get<CancelledNotification>();
        } else if (method == ProgressNotification::method_str) {
            n.data_ = j.get<ProgressNotification>();
        } else if (method == InitializedNotification::method_str) {
            n.data_ = j.get<InitializedNotification>();
        } else if (method == RootsListChangedNotification::method_str) {
            n.data_ = j.get<RootsListChangedNotification>();
        } else {
            n.data_ = j.get<CustomNotification>();
        }
    }

private:
    Variant data_;
};

using ClientNotification = ClientNotificationVariant;

// =============================================================================
// ClientResult variant (untagged - try each variant)
// =============================================================================

class ClientResultVariant {
public:
    using Variant = std::variant<
        CreateMessageResult,
        ListRootsResult,
        CreateElicitationResult,
        EmptyResult,
        CustomResult
    >;

    ClientResultVariant() : data_(EmptyResult{}) {}

    template <typename T>
    ClientResultVariant(T val) : data_(std::move(val)) {}

    template <typename T>
    bool is() const { return std::holds_alternative<T>(data_); }

    template <typename T>
    const T& get() const { return std::get<T>(data_); }

    static ClientResultVariant empty() { return ClientResultVariant(EmptyResult{}); }

    const Variant& variant() const { return data_; }

    bool operator==(const ClientResultVariant& other) const { return data_ == other.data_; }

    friend void to_json(json& j, const ClientResultVariant& r) {
        std::visit([&j](const auto& v) { j = v; }, r.data_);
    }

    friend void from_json(const json& j, ClientResultVariant& r) {
        // Try each variant type (untagged deserialization)
        try { r.data_ = j.get<CreateMessageResult>(); return; } catch (...) {}
        try { r.data_ = j.get<ListRootsResult>(); return; } catch (...) {}
        try { r.data_ = j.get<CreateElicitationResult>(); return; } catch (...) {}
        // EmptyResult matches empty objects
        if (j.is_object() && j.empty()) {
            r.data_ = EmptyResult{};
            return;
        }
        // Fall back to CustomResult
        r.data_ = CustomResult(j);
    }

private:
    Variant data_;
};

using ClientResult = ClientResultVariant;

// =============================================================================
// ServerRequest variant
// =============================================================================

class ServerRequestVariant {
public:
    using Variant = std::variant<
        PingRequest,
        CreateMessageRequest,
        ListRootsRequest,
        CreateElicitationRequest,
        CustomRequest
    >;

    ServerRequestVariant() : data_(PingRequest{}) {}

    template <typename T>
    ServerRequestVariant(T val) : data_(std::move(val)) {}

    template <typename T>
    bool is() const { return std::holds_alternative<T>(data_); }

    template <typename T>
    const T& get() const { return std::get<T>(data_); }

    std::string method() const {
        return std::visit([](const auto& v) -> std::string {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, CustomRequest>) {
                return v.method;
            } else {
                return v.method();
            }
        }, data_);
    }

    const Variant& variant() const { return data_; }

    bool operator==(const ServerRequestVariant& other) const { return data_ == other.data_; }

    friend void to_json(json& j, const ServerRequestVariant& r) {
        std::visit([&j](const auto& v) { j = v; }, r.data_);
    }

    friend void from_json(const json& j, ServerRequestVariant& r) {
        if (!j.contains("method")) {
            throw json::other_error::create(
                501, "ServerRequest missing 'method' field", &j);
        }
        auto method = j.at("method").get<std::string>();

        if (method == PingRequest::method_str) {
            r.data_ = j.get<PingRequest>();
        } else if (method == CreateMessageRequest::method_str) {
            r.data_ = j.get<CreateMessageRequest>();
        } else if (method == ListRootsRequest::method_str) {
            r.data_ = j.get<ListRootsRequest>();
        } else if (method == CreateElicitationRequest::method_str) {
            r.data_ = j.get<CreateElicitationRequest>();
        } else {
            r.data_ = j.get<CustomRequest>();
        }
    }

private:
    Variant data_;
};

using ServerRequest = ServerRequestVariant;

// =============================================================================
// ServerNotification variant
// =============================================================================

class ServerNotificationVariant {
public:
    using Variant = std::variant<
        CancelledNotification,
        ProgressNotification,
        LoggingMessageNotification,
        ResourceUpdatedNotification,
        ResourceListChangedNotification,
        ToolListChangedNotification,
        PromptListChangedNotification,
        CustomNotification
    >;

    ServerNotificationVariant() : data_(CustomNotification{}) {}

    template <typename T>
    ServerNotificationVariant(T val) : data_(std::move(val)) {}

    template <typename T>
    bool is() const { return std::holds_alternative<T>(data_); }

    template <typename T>
    const T& get() const { return std::get<T>(data_); }

    std::string method() const {
        return std::visit([](const auto& v) -> std::string {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, CustomNotification>) {
                return v.method;
            } else {
                return v.method();
            }
        }, data_);
    }

    const Variant& variant() const { return data_; }

    bool operator==(const ServerNotificationVariant& other) const {
        return data_ == other.data_;
    }

    friend void to_json(json& j, const ServerNotificationVariant& n) {
        std::visit([&j](const auto& v) { j = v; }, n.data_);
    }

    friend void from_json(const json& j, ServerNotificationVariant& n) {
        if (!j.contains("method")) {
            throw json::other_error::create(
                501, "ServerNotification missing 'method' field", &j);
        }
        auto method = j.at("method").get<std::string>();

        if (method == CancelledNotification::method_str) {
            n.data_ = j.get<CancelledNotification>();
        } else if (method == ProgressNotification::method_str) {
            n.data_ = j.get<ProgressNotification>();
        } else if (method == LoggingMessageNotification::method_str) {
            n.data_ = j.get<LoggingMessageNotification>();
        } else if (method == ResourceUpdatedNotification::method_str) {
            n.data_ = j.get<ResourceUpdatedNotification>();
        } else if (method == ResourceListChangedNotification::method_str) {
            n.data_ = j.get<ResourceListChangedNotification>();
        } else if (method == ToolListChangedNotification::method_str) {
            n.data_ = j.get<ToolListChangedNotification>();
        } else if (method == PromptListChangedNotification::method_str) {
            n.data_ = j.get<PromptListChangedNotification>();
        } else {
            n.data_ = j.get<CustomNotification>();
        }
    }

private:
    Variant data_;
};

using ServerNotification = ServerNotificationVariant;

// =============================================================================
// ServerResult variant (untagged - try each variant)
// =============================================================================

class ServerResultVariant {
public:
    using Variant = std::variant<
        InitializeResult,
        CompleteResult,
        GetPromptResult,
        ListPromptsResult,
        ListResourcesResult,
        ListResourceTemplatesResult,
        ReadResourceResult,
        CallToolResult,
        ListToolsResult,
        CreateElicitationResult,
        EmptyResult,
        CreateTaskResult,
        ListTasksResult,
        GetTaskInfoResult,
        TaskResult,
        CustomResult
    >;

    ServerResultVariant() : data_(EmptyResult{}) {}

    template <typename T>
    ServerResultVariant(T val) : data_(std::move(val)) {}

    template <typename T>
    bool is() const { return std::holds_alternative<T>(data_); }

    template <typename T>
    const T& get() const { return std::get<T>(data_); }

    static ServerResultVariant empty() { return ServerResultVariant(EmptyResult{}); }

    const Variant& variant() const { return data_; }

    bool operator==(const ServerResultVariant& other) const { return data_ == other.data_; }

    friend void to_json(json& j, const ServerResultVariant& r) {
        std::visit([&j](const auto& v) { j = v; }, r.data_);
    }

    friend void from_json(const json& j, ServerResultVariant& r) {
        // Try each variant type (untagged deserialization)
        // Order matters: try more specific types first.
        try { r.data_ = j.get<InitializeResult>(); return; } catch (...) {}
        try { r.data_ = j.get<CompleteResult>(); return; } catch (...) {}
        try { r.data_ = j.get<GetPromptResult>(); return; } catch (...) {}
        try { r.data_ = j.get<ListPromptsResult>(); return; } catch (...) {}
        try { r.data_ = j.get<ListResourcesResult>(); return; } catch (...) {}
        try { r.data_ = j.get<ListResourceTemplatesResult>(); return; } catch (...) {}
        try { r.data_ = j.get<ReadResourceResult>(); return; } catch (...) {}
        try { r.data_ = j.get<ListToolsResult>(); return; } catch (...) {}
        try { r.data_ = j.get<CallToolResult>(); return; } catch (...) {}
        try { r.data_ = j.get<CreateElicitationResult>(); return; } catch (...) {}
        try { r.data_ = j.get<CreateTaskResult>(); return; } catch (...) {}
        try { r.data_ = j.get<ListTasksResult>(); return; } catch (...) {}
        try { r.data_ = j.get<GetTaskInfoResult>(); return; } catch (...) {}
        try { r.data_ = j.get<TaskResult>(); return; } catch (...) {}
        // EmptyResult matches empty objects
        if (j.is_object() && j.empty()) {
            r.data_ = EmptyResult{};
            return;
        }
        // Fall back to CustomResult
        r.data_ = CustomResult(j);
    }

private:
    Variant data_;
};

using ServerResult = ServerResultVariant;

// =============================================================================
// Typed JsonRpcMessage aliases
// =============================================================================

using ClientJsonRpcMessage = JsonRpcMessage<ClientRequest, ClientResult, ClientNotification>;
using ServerJsonRpcMessage = JsonRpcMessage<ServerRequest, ServerResult, ServerNotification>;

// =============================================================================
// Cleanup macros
// =============================================================================

#undef MCP_DEFINE_REQUEST
#undef MCP_REQUEST_PARAMS_FIELD_REQUIRED
#undef MCP_REQUEST_PARAMS_FIELD_OPTIONAL
#undef MCP_REQUEST_PARAMS_FIELD_NONE
#undef MCP_REQUEST_EQ_REQUIRED
#undef MCP_REQUEST_EQ_OPTIONAL
#undef MCP_REQUEST_EQ_NONE
#undef MCP_REQUEST_TO_JSON_REQUIRED
#undef MCP_REQUEST_TO_JSON_OPTIONAL
#undef MCP_REQUEST_TO_JSON_NONE
#undef MCP_REQUEST_FROM_JSON_REQUIRED
#undef MCP_REQUEST_FROM_JSON_OPTIONAL
#undef MCP_REQUEST_FROM_JSON_NONE

} // namespace mcp
