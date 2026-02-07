#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "mcp/model/jsonrpc.hpp"
#include "mcp/model/unions.hpp"

using namespace mcp;
using json = nlohmann::json;

TEST(JsonRpc, RequestSerialize) {
    auto msg = ClientJsonRpcMessage::request(
        ClientRequest(PingRequest{}),
        RequestId(int64_t{1}));
    json j = msg;
    EXPECT_EQ(j["jsonrpc"], "2.0");
    EXPECT_EQ(j["id"], 1);
    EXPECT_EQ(j["method"], "ping");
}

TEST(JsonRpc, ResponseSerialize) {
    auto msg = ServerJsonRpcMessage::response(
        ServerResult(EmptyResult{}),
        RequestId(int64_t{1}));
    json j = msg;
    EXPECT_EQ(j["jsonrpc"], "2.0");
    EXPECT_EQ(j["id"], 1);
    EXPECT_TRUE(j.contains("result"));
}

TEST(JsonRpc, ErrorSerialize) {
    auto msg = ServerJsonRpcMessage::error(
        ErrorData::method_not_found("test"),
        RequestId(int64_t{1}));
    json j = msg;
    EXPECT_EQ(j["jsonrpc"], "2.0");
    EXPECT_EQ(j["id"], 1);
    EXPECT_EQ(j["error"]["code"], ErrorCode::METHOD_NOT_FOUND.code);
    EXPECT_EQ(j["error"]["message"], "test");
}

TEST(JsonRpc, NotificationSerialize) {
    auto msg = ClientJsonRpcMessage::notification(
        ClientNotification(InitializedNotification{}));
    json j = msg;
    EXPECT_EQ(j["jsonrpc"], "2.0");
    EXPECT_EQ(j["method"], "notifications/initialized");
    EXPECT_FALSE(j.contains("id"));
}

TEST(JsonRpc, RequestDeserialize) {
    json j = {{"jsonrpc", "2.0"}, {"id", 42}, {"method", "ping"}};
    auto msg = j.get<ClientJsonRpcMessage>();
    EXPECT_TRUE(msg.is_request());
    EXPECT_EQ(msg.as_request().id, RequestId(int64_t{42}));
    EXPECT_TRUE(msg.as_request().request.is<PingRequest>());
}

TEST(JsonRpc, ResponseDeserialize) {
    json j = {{"jsonrpc", "2.0"}, {"id", 1}, {"result", json::object()}};
    auto msg = j.get<ServerJsonRpcMessage>();
    EXPECT_TRUE(msg.is_response());
}

TEST(JsonRpc, ErrorDeserialize) {
    json j = {{"jsonrpc", "2.0"}, {"id", 1}, {"error", {{"code", -32601}, {"message", "not found"}}}};
    auto msg = j.get<ServerJsonRpcMessage>();
    EXPECT_TRUE(msg.is_error());
    EXPECT_EQ(msg.as_error().error.code, ErrorCode::METHOD_NOT_FOUND);
}

TEST(JsonRpc, NotificationDeserialize) {
    json j = {{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}};
    auto msg = j.get<ClientJsonRpcMessage>();
    EXPECT_TRUE(msg.is_notification());
    EXPECT_TRUE(msg.as_notification().notification.is<InitializedNotification>());
}

TEST(JsonRpc, CallToolRequestRoundTrip) {
    CallToolRequestParams params("my_tool");
    params.arguments = JsonObject{{"key", "value"}};
    auto req = CallToolRequest{};
    req.params = params;
    auto msg = ClientJsonRpcMessage::request(ClientRequest(req), RequestId(int64_t{5}));
    json j = msg;
    EXPECT_EQ(j["method"], "tools/call");
    EXPECT_EQ(j["params"]["name"], "my_tool");
    EXPECT_EQ(j["params"]["arguments"]["key"], "value");

    auto rt = j.get<ClientJsonRpcMessage>();
    EXPECT_TRUE(rt.is_request());
    EXPECT_TRUE(rt.as_request().request.is<CallToolRequest>());
    auto& rt_params = rt.as_request().request.get<CallToolRequest>().params;
    EXPECT_EQ(rt_params.name, "my_tool");
}

TEST(JsonRpc, ListToolsResponseRoundTrip) {
    ListToolsResult lt;
    Tool t;
    t.name = "test_tool";
    t.description = "A test";
    lt.tools.push_back(t);
    auto msg = ServerJsonRpcMessage::response(ServerResult(lt), RequestId(int64_t{2}));
    json j = msg;
    EXPECT_TRUE(j["result"].contains("tools"));
    EXPECT_EQ(j["result"]["tools"][0]["name"], "test_tool");
}

TEST(JsonRpc, MessageDiscrimination) {
    // Request: has id + method
    json req = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "ping"}};
    auto m1 = req.get<ClientJsonRpcMessage>();
    EXPECT_TRUE(m1.is_request());

    // Response: has id + result
    json resp = {{"jsonrpc", "2.0"}, {"id", 1}, {"result", json::object()}};
    auto m2 = resp.get<ServerJsonRpcMessage>();
    EXPECT_TRUE(m2.is_response());

    // Error: has id + error
    json err = {{"jsonrpc", "2.0"}, {"id", 1}, {"error", {{"code", -32600}, {"message", "bad"}}}};
    auto m3 = err.get<ServerJsonRpcMessage>();
    EXPECT_TRUE(m3.is_error());

    // Notification: has method, no id
    json noti = {{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}};
    auto m4 = noti.get<ClientJsonRpcMessage>();
    EXPECT_TRUE(m4.is_notification());
}
