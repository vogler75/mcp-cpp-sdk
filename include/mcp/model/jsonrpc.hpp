#pragma once

#include <optional>
#include <string>
#include <variant>

#include "mcp/model/types.hpp"

namespace mcp {

// =============================================================================
// Default (generic) Request and Notification for untyped usage
// =============================================================================

struct GenericRequest {
    std::string method;
    JsonObject params;

    bool operator==(const GenericRequest& other) const {
        return method == other.method && params == other.params;
    }

    friend void to_json(json& j, const GenericRequest& r) {
        j = json{{"method", r.method}};
        // Merge params at top level
        for (auto& [k, v] : r.params) {
            j[k] = v;
        }
    }

    friend void from_json(const json& j, GenericRequest& r) {
        j.at("method").get_to(r.method);
        if (j.contains("params") && j["params"].is_object()) {
            r.params = j["params"].get<JsonObject>();
        }
    }
};

struct GenericNotification {
    std::string method;
    JsonObject params;

    bool operator==(const GenericNotification& other) const {
        return method == other.method && params == other.params;
    }

    friend void to_json(json& j, const GenericNotification& n) {
        j = json{{"method", n.method}};
        for (auto& [k, v] : n.params) {
            j[k] = v;
        }
    }

    friend void from_json(const json& j, GenericNotification& n) {
        j.at("method").get_to(n.method);
        if (j.contains("params") && j["params"].is_object()) {
            n.params = j["params"].get<JsonObject>();
        }
    }
};

// =============================================================================
// JsonRpcRequest<R>
//
// JSON-RPC 2.0 request wrapper. The request R is FLATTENED: its method/params
// fields are merged into the top-level JSON alongside jsonrpc and id.
// =============================================================================

template <typename R = GenericRequest>
struct JsonRpcRequest {
    std::string jsonrpc = "2.0";
    RequestId id;
    R request;

    bool operator==(const JsonRpcRequest& other) const {
        return id == other.id && request == other.request;
    }

    friend void to_json(json& j, const JsonRpcRequest& r) {
        // Start with the request fields (flattened)
        j = r.request;
        j["jsonrpc"] = r.jsonrpc;
        j["id"] = r.id;
    }

    friend void from_json(const json& j, JsonRpcRequest& r) {
        j.at("jsonrpc").get_to(r.jsonrpc);
        j.at("id").get_to(r.id);
        r.request = j.get<R>();
    }
};

// =============================================================================
// JsonRpcResponse<R>
// =============================================================================

template <typename R = json>
struct JsonRpcResponse {
    std::string jsonrpc = "2.0";
    RequestId id;
    R result;

    bool operator==(const JsonRpcResponse& other) const {
        return id == other.id && result == other.result;
    }

    friend void to_json(json& j, const JsonRpcResponse& r) {
        j = json{{"jsonrpc", r.jsonrpc}, {"id", r.id}, {"result", r.result}};
    }

    friend void from_json(const json& j, JsonRpcResponse& r) {
        j.at("jsonrpc").get_to(r.jsonrpc);
        j.at("id").get_to(r.id);
        j.at("result").get_to(r.result);
    }
};

// =============================================================================
// JsonRpcError
// =============================================================================

struct JsonRpcError {
    std::string jsonrpc = "2.0";
    RequestId id;
    ErrorData error;

    bool operator==(const JsonRpcError& other) const {
        return id == other.id && error == other.error;
    }

    friend void to_json(json& j, const JsonRpcError& e) {
        j = json{{"jsonrpc", e.jsonrpc}, {"id", e.id}, {"error", e.error}};
    }

    friend void from_json(const json& j, JsonRpcError& e) {
        j.at("jsonrpc").get_to(e.jsonrpc);
        j.at("id").get_to(e.id);
        j.at("error").get_to(e.error);
    }
};

// =============================================================================
// JsonRpcNotification<N>
//
// Notification is FLATTENED: method/params from N are at the top level.
// =============================================================================

template <typename N = GenericNotification>
struct JsonRpcNotification {
    std::string jsonrpc = "2.0";
    N notification;

    bool operator==(const JsonRpcNotification& other) const {
        return notification == other.notification;
    }

    friend void to_json(json& j, const JsonRpcNotification& n) {
        // Start with the notification fields (flattened)
        j = n.notification;
        j["jsonrpc"] = n.jsonrpc;
    }

    friend void from_json(const json& j, JsonRpcNotification& n) {
        j.at("jsonrpc").get_to(n.jsonrpc);
        n.notification = j.get<N>();
    }
};

// =============================================================================
// JsonRpcMessage<Req, Resp, Noti>
//
// A variant of Request | Response | Notification | Error.
// Deserialization uses field presence to discriminate:
//   - "id" + "method"  -> Request
//   - "id" + "result"  -> Response
//   - "id" + "error"   -> Error
//   - "method" (no id) -> Notification
// =============================================================================

template <
    typename Req = GenericRequest,
    typename Resp = json,
    typename Noti = GenericNotification
>
class JsonRpcMessage {
public:
    using Request = JsonRpcRequest<Req>;
    using Response = JsonRpcResponse<Resp>;
    using Notification = JsonRpcNotification<Noti>;
    using Error = JsonRpcError;
    using Variant = std::variant<Request, Response, Notification, Error>;

    JsonRpcMessage() : data_(Notification{}) {}
    JsonRpcMessage(Request r) : data_(std::move(r)) {}
    JsonRpcMessage(Response r) : data_(std::move(r)) {}
    JsonRpcMessage(Notification n) : data_(std::move(n)) {}
    JsonRpcMessage(Error e) : data_(std::move(e)) {}

    // --- Variant queries ---

    bool is_request() const { return std::holds_alternative<Request>(data_); }
    bool is_response() const { return std::holds_alternative<Response>(data_); }
    bool is_notification() const { return std::holds_alternative<Notification>(data_); }
    bool is_error() const { return std::holds_alternative<Error>(data_); }

    const Request& as_request() const { return std::get<Request>(data_); }
    const Response& as_response() const { return std::get<Response>(data_); }
    const Notification& as_notification() const { return std::get<Notification>(data_); }
    const Error& as_error() const { return std::get<Error>(data_); }

    Request& as_request() { return std::get<Request>(data_); }
    Response& as_response() { return std::get<Response>(data_); }
    Notification& as_notification() { return std::get<Notification>(data_); }
    Error& as_error() { return std::get<Error>(data_); }

    // --- Static factories ---

    static JsonRpcMessage request(Req req, RequestId id) {
        Request r;
        r.id = std::move(id);
        r.request = std::move(req);
        return JsonRpcMessage(std::move(r));
    }

    static JsonRpcMessage response(Resp resp, RequestId id) {
        Response r;
        r.id = std::move(id);
        r.result = std::move(resp);
        return JsonRpcMessage(std::move(r));
    }

    static JsonRpcMessage error(ErrorData err, RequestId id) {
        Error e;
        e.id = std::move(id);
        e.error = std::move(err);
        return JsonRpcMessage(std::move(e));
    }

    static JsonRpcMessage notification(Noti noti) {
        Notification n;
        n.notification = std::move(noti);
        return JsonRpcMessage(std::move(n));
    }

    // --- Extractors ---

    std::optional<std::pair<Req, RequestId>> into_request() {
        if (!is_request()) return std::nullopt;
        auto& r = as_request();
        return std::make_pair(std::move(r.request), std::move(r.id));
    }

    std::optional<std::pair<Resp, RequestId>> into_response() {
        if (!is_response()) return std::nullopt;
        auto& r = as_response();
        return std::make_pair(std::move(r.result), std::move(r.id));
    }

    std::optional<Noti> into_notification() {
        if (!is_notification()) return std::nullopt;
        return std::move(as_notification().notification);
    }

    std::optional<std::pair<ErrorData, RequestId>> into_error() {
        if (!is_error()) return std::nullopt;
        auto& e = as_error();
        return std::make_pair(std::move(e.error), std::move(e.id));
    }

    /// Extract either a successful response or an error, both keyed by request id.
    std::optional<std::pair<std::variant<Resp, ErrorData>, RequestId>> into_result() {
        if (is_response()) {
            auto& r = as_response();
            return std::make_pair(
                std::variant<Resp, ErrorData>(std::move(r.result)),
                std::move(r.id)
            );
        }
        if (is_error()) {
            auto& e = as_error();
            return std::make_pair(
                std::variant<Resp, ErrorData>(std::move(e.error)),
                std::move(e.id)
            );
        }
        return std::nullopt;
    }

    bool operator==(const JsonRpcMessage& other) const { return data_ == other.data_; }

    // --- Serialization ---

    friend void to_json(json& j, const JsonRpcMessage& m) {
        std::visit([&j](const auto& v) { j = v; }, m.data_);
    }

    friend void from_json(const json& j, JsonRpcMessage& m) {
        bool has_id = j.contains("id");
        bool has_method = j.contains("method");
        bool has_result = j.contains("result");
        bool has_error = j.contains("error");

        if (has_id && has_method) {
            // Request
            Request r;
            from_json(j, r);
            m.data_ = std::move(r);
        } else if (has_id && has_result) {
            // Response
            Response r;
            from_json(j, r);
            m.data_ = std::move(r);
        } else if (has_id && has_error) {
            // Error
            Error e;
            from_json(j, e);
            m.data_ = std::move(e);
        } else if (has_method && !has_id) {
            // Notification
            Notification n;
            from_json(j, n);
            m.data_ = std::move(n);
        } else {
            throw json::other_error::create(
                501,
                "Cannot determine JSON-RPC message type: missing discriminating fields",
                &j
            );
        }
    }

private:
    Variant data_;
};

// =============================================================================
// Default untyped JsonRpcMessage
// =============================================================================

using DefaultJsonRpcMessage = JsonRpcMessage<GenericRequest, json, GenericNotification>;

} // namespace mcp
