#pragma once

#include <optional>
#include <string>

#include "mcp/model/types.hpp"
#include "mcp/model/meta.hpp"

namespace mcp {

// =============================================================================
// LoggingLevel
// =============================================================================

enum class LoggingLevel {
    Debug,
    Info,
    Notice,
    Warning,
    Error,
    Critical,
    Alert,
    Emergency,
};

inline void to_json(json& j, const LoggingLevel& l) {
    switch (l) {
        case LoggingLevel::Debug: j = "debug"; break;
        case LoggingLevel::Info: j = "info"; break;
        case LoggingLevel::Notice: j = "notice"; break;
        case LoggingLevel::Warning: j = "warning"; break;
        case LoggingLevel::Error: j = "error"; break;
        case LoggingLevel::Critical: j = "critical"; break;
        case LoggingLevel::Alert: j = "alert"; break;
        case LoggingLevel::Emergency: j = "emergency"; break;
    }
}

inline void from_json(const json& j, LoggingLevel& l) {
    auto s = j.get<std::string>();
    if (s == "debug") {
        l = LoggingLevel::Debug;
    } else if (s == "info") {
        l = LoggingLevel::Info;
    } else if (s == "notice") {
        l = LoggingLevel::Notice;
    } else if (s == "warning") {
        l = LoggingLevel::Warning;
    } else if (s == "error") {
        l = LoggingLevel::Error;
    } else if (s == "critical") {
        l = LoggingLevel::Critical;
    } else if (s == "alert") {
        l = LoggingLevel::Alert;
    } else if (s == "emergency") {
        l = LoggingLevel::Emergency;
    } else {
        throw json::other_error::create(501, "Unknown LoggingLevel: " + s, &j);
    }
}

// =============================================================================
// SetLevelRequestParams
// =============================================================================

struct SetLevelRequestParams {
    std::optional<Meta> meta;
    LoggingLevel level;

    bool operator==(const SetLevelRequestParams& other) const {
        return meta == other.meta && level == other.level;
    }

    friend void to_json(json& j, const SetLevelRequestParams& p) {
        j = json{{"level", p.level}};
        if (p.meta.has_value()) {
            j["_meta"] = *p.meta;
        }
    }

    friend void from_json(const json& j, SetLevelRequestParams& p) {
        j.at("level").get_to(p.level);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            p.meta = j["_meta"].get<Meta>();
        }
    }
};

// =============================================================================
// LoggingMessageNotificationParam
// =============================================================================

struct LoggingMessageNotificationParam {
    LoggingLevel level;
    std::optional<std::string> logger;
    json data;

    bool operator==(const LoggingMessageNotificationParam& other) const {
        return level == other.level && logger == other.logger && data == other.data;
    }

    friend void to_json(json& j, const LoggingMessageNotificationParam& p) {
        j = json{{"level", p.level}, {"data", p.data}};
        detail::set_opt(j, "logger", p.logger);
    }

    friend void from_json(const json& j, LoggingMessageNotificationParam& p) {
        j.at("level").get_to(p.level);
        j.at("data").get_to(p.data);
        detail::get_opt(j, "logger", p.logger);
    }
};

} // namespace mcp
