#pragma once

#include <optional>
#include <string>

#include "mcp/model/types.hpp"
#include "mcp/model/logging.hpp"

namespace mcp {

// =============================================================================
// CancelledNotificationParam
// =============================================================================

struct CancelledNotificationParam {
    RequestId request_id;
    std::optional<std::string> reason;

    bool operator==(const CancelledNotificationParam& other) const {
        return request_id == other.request_id && reason == other.reason;
    }

    friend void to_json(json& j, const CancelledNotificationParam& p) {
        j = json{{"requestId", p.request_id}};
        detail::set_opt(j, "reason", p.reason);
    }

    friend void from_json(const json& j, CancelledNotificationParam& p) {
        j.at("requestId").get_to(p.request_id);
        detail::get_opt(j, "reason", p.reason);
    }
};

// =============================================================================
// ProgressNotificationParam
// =============================================================================

struct ProgressNotificationParam {
    ProgressToken progress_token;
    double progress;
    std::optional<double> total;
    std::optional<std::string> message;

    bool operator==(const ProgressNotificationParam& other) const {
        return progress_token == other.progress_token && progress == other.progress
            && total == other.total && message == other.message;
    }

    friend void to_json(json& j, const ProgressNotificationParam& p) {
        j = json{{"progressToken", p.progress_token}, {"progress", p.progress}};
        detail::set_opt(j, "total", p.total);
        detail::set_opt(j, "message", p.message);
    }

    friend void from_json(const json& j, ProgressNotificationParam& p) {
        j.at("progressToken").get_to(p.progress_token);
        j.at("progress").get_to(p.progress);
        detail::get_opt(j, "total", p.total);
        detail::get_opt(j, "message", p.message);
    }
};

// =============================================================================
// ResourceUpdatedNotificationParam
// =============================================================================

struct ResourceUpdatedNotificationParam {
    std::string uri;

    bool operator==(const ResourceUpdatedNotificationParam& other) const {
        return uri == other.uri;
    }

    friend void to_json(json& j, const ResourceUpdatedNotificationParam& p) {
        j = json{{"uri", p.uri}};
    }

    friend void from_json(const json& j, ResourceUpdatedNotificationParam& p) {
        j.at("uri").get_to(p.uri);
    }
};

// Note: LoggingMessageNotificationParam is defined in logging.hpp

// =============================================================================
// CustomNotification
// =============================================================================

struct CustomNotification {
    std::string method;
    std::optional<json> params;

    bool operator==(const CustomNotification& other) const {
        return method == other.method && params == other.params;
    }

    friend void to_json(json& j, const CustomNotification& n) {
        j = json{{"method", n.method}};
        if (n.params.has_value()) {
            j["params"] = *n.params;
        }
    }

    friend void from_json(const json& j, CustomNotification& n) {
        j.at("method").get_to(n.method);
        if (j.contains("params") && !j["params"].is_null()) {
            n.params = j["params"];
        }
    }
};

// =============================================================================
// CustomRequest
// =============================================================================

struct CustomRequest {
    std::string method;
    std::optional<json> params;

    bool operator==(const CustomRequest& other) const {
        return method == other.method && params == other.params;
    }

    friend void to_json(json& j, const CustomRequest& r) {
        j = json{{"method", r.method}};
        if (r.params.has_value()) {
            j["params"] = *r.params;
        }
    }

    friend void from_json(const json& j, CustomRequest& r) {
        j.at("method").get_to(r.method);
        if (j.contains("params") && !j["params"].is_null()) {
            r.params = j["params"];
        }
    }
};

} // namespace mcp
