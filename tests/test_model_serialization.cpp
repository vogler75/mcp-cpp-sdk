#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "mcp/model/types.hpp"
#include "mcp/model/error.hpp"
#include "mcp/model/content.hpp"
#include "mcp/model/tool.hpp"
#include "mcp/model/resource.hpp"
#include "mcp/model/prompt.hpp"
#include "mcp/model/capabilities.hpp"
#include "mcp/model/sampling.hpp"
#include "mcp/model/init.hpp"

using namespace mcp;
using json = nlohmann::json;

TEST(ModelSerialization, ProtocolVersionRoundTrip) {
    json j = ProtocolVersion::LATEST;
    auto rt = j.get<ProtocolVersion>();
    EXPECT_EQ(rt, ProtocolVersion::LATEST);
}

TEST(ModelSerialization, NumberOrStringInt) {
    NumberOrString nos(int64_t{42});
    json j = nos;
    auto rt = j.get<NumberOrString>();
    EXPECT_TRUE(rt.is_number());
    EXPECT_EQ(rt.as_number(), 42);
}

TEST(ModelSerialization, NumberOrStringStr) {
    NumberOrString nos(std::string("hello"));
    json j = nos;
    auto rt = j.get<NumberOrString>();
    EXPECT_TRUE(rt.is_string());
    EXPECT_EQ(rt.as_string(), "hello");
}

TEST(ModelSerialization, ErrorCodeValues) {
    EXPECT_EQ(ErrorCode::PARSE_ERROR.code, -32700);
    EXPECT_EQ(ErrorCode::INVALID_REQUEST.code, -32600);
    EXPECT_EQ(ErrorCode::METHOD_NOT_FOUND.code, -32601);
    EXPECT_EQ(ErrorCode::INVALID_PARAMS.code, -32602);
    EXPECT_EQ(ErrorCode::INTERNAL_ERROR.code, -32603);
    EXPECT_EQ(ErrorCode::REQUEST_TIMEOUT.code, -32001);
}

TEST(ModelSerialization, ImplementationRoundTrip) {
    Implementation impl;
    impl.name = "test-server";
    impl.version = "1.0.0";
    json j = impl;
    EXPECT_EQ(j["name"], "test-server");
    EXPECT_EQ(j["version"], "1.0.0");
    auto rt = j.get<Implementation>();
    EXPECT_EQ(rt.name, "test-server");
    EXPECT_EQ(rt.version, "1.0.0");
}

TEST(ModelSerialization, EmptyObject) {
    EmptyObject eo;
    json j = eo;
    EXPECT_TRUE(j.is_object());
    EXPECT_TRUE(j.empty());
}

// ---- ErrorData ----

TEST(ModelSerialization, ErrorDataRoundTrip) {
    auto ed = ErrorData::method_not_found("foo/bar");
    json j = ed;
    EXPECT_EQ(j["code"], ErrorCode::METHOD_NOT_FOUND.code);
    EXPECT_EQ(j["message"], "foo/bar");
    auto rt = j.get<ErrorData>();
    EXPECT_EQ(rt.code, ErrorCode::METHOD_NOT_FOUND);
    EXPECT_EQ(rt.message, "foo/bar");
    EXPECT_FALSE(rt.data.has_value());
}

TEST(ModelSerialization, ErrorDataWithData) {
    auto ed = ErrorData::internal_error("oops", json{{"detail", "stack trace"}});
    json j = ed;
    EXPECT_TRUE(j.contains("data"));
    EXPECT_EQ(j["data"]["detail"], "stack trace");
    auto rt = j.get<ErrorData>();
    EXPECT_TRUE(rt.data.has_value());
    EXPECT_EQ((*rt.data)["detail"], "stack trace");
}

// ---- RawContent ----

TEST(ModelSerialization, RawContentTextRoundTrip) {
    auto c = RawContent::text("Hello world");
    json j = c;
    EXPECT_EQ(j["type"], "text");
    EXPECT_EQ(j["text"], "Hello world");
    auto rt = j.get<RawContent>();
    EXPECT_TRUE(rt.is_text());
    EXPECT_EQ(rt.as_text()->text, "Hello world");
}

TEST(ModelSerialization, RawContentImageRoundTrip) {
    auto c = RawContent::image("base64data", "image/png");
    json j = c;
    EXPECT_EQ(j["type"], "image");
    EXPECT_EQ(j["data"], "base64data");
    EXPECT_EQ(j["mimeType"], "image/png");
    auto rt = j.get<RawContent>();
    EXPECT_TRUE(rt.is_image());
    EXPECT_EQ(rt.as_image()->data, "base64data");
    EXPECT_EQ(rt.as_image()->mime_type, "image/png");
}

// ---- Tool ----

TEST(ModelSerialization, ToolRoundTrip) {
    Tool tool;
    tool.name = "my_tool";
    tool.description = "A test tool";
    json j = tool;
    EXPECT_EQ(j["name"], "my_tool");
    EXPECT_EQ(j["description"], "A test tool");
    auto rt = j.get<Tool>();
    EXPECT_EQ(rt.name, "my_tool");
    EXPECT_TRUE(rt.description.has_value());
    EXPECT_EQ(*rt.description, "A test tool");
}

// ---- Prompt ----

TEST(ModelSerialization, PromptRoundTrip) {
    Prompt p("test_prompt", "A test prompt");
    json j = p;
    EXPECT_EQ(j["name"], "test_prompt");
    EXPECT_EQ(j["description"], "A test prompt");
    auto rt = j.get<Prompt>();
    EXPECT_EQ(rt.name, "test_prompt");
}

// ---- Capabilities ----

TEST(ModelSerialization, ServerCapabilitiesEmpty) {
    ServerCapabilities caps;
    json j = caps;
    EXPECT_TRUE(j.is_object());
    // No fields set - should be mostly empty
    auto rt = j.get<ServerCapabilities>();
    EXPECT_FALSE(rt.tools.has_value());
}

// ---- InitializeRequestParams / InitializeResult ----

TEST(ModelSerialization, InitializeRequestParamsRoundTrip) {
    InitializeRequestParams params;
    params.protocol_version = ProtocolVersion::LATEST;
    params.client_info.name = "test-client";
    params.client_info.version = "1.0.0";
    json j = params;
    EXPECT_EQ(j["protocolVersion"], "2025-03-26");
    EXPECT_EQ(j["clientInfo"]["name"], "test-client");
    auto rt = j.get<InitializeRequestParams>();
    EXPECT_EQ(rt.protocol_version, ProtocolVersion::LATEST);
    EXPECT_EQ(rt.client_info.name, "test-client");
}

TEST(ModelSerialization, InitializeResultRoundTrip) {
    InitializeResult result;
    result.protocol_version = ProtocolVersion::LATEST;
    result.server_info.name = "test-server";
    result.server_info.version = "2.0.0";
    json j = result;
    EXPECT_EQ(j["protocolVersion"], "2025-03-26");
    EXPECT_EQ(j["serverInfo"]["name"], "test-server");
    auto rt = j.get<InitializeResult>();
    EXPECT_EQ(rt.protocol_version, ProtocolVersion::LATEST);
    EXPECT_EQ(rt.server_info.name, "test-server");
}

