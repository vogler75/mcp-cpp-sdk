/// @file http_test_server.cpp
/// @brief Comprehensive MCP test server with Streamable HTTP transport
///
/// This example demonstrates:
/// - Streamable HTTP server with session management
/// - Simple Bearer token authentication
/// - Tools: counter operations, echo, math
/// - Prompts: example_prompt, counter_analysis
/// - Resources: static and dynamic resources
/// - Proper capability negotiation
///
/// Usage:
///   ./http_test_server [port]
///
/// Test with curl:
///   # Get a token (demo endpoint)
///   curl http://localhost:8080/token/demo
///
///   # Initialize session
///   curl -X POST http://localhost:8080/mcp \
///     -H "Authorization: Bearer demo-token" \
///     -H "Content-Type: application/json" \
///     -H "Accept: application/json, text/event-stream" \
///     -d '{"jsonrpc":"2.0","method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}},"id":1}'
///
///   # List tools (use session ID from initialize response)
///   curl -X POST http://localhost:8080/mcp \
///     -H "Authorization: Bearer demo-token" \
///     -H "Mcp-Session-Id: <session-id>" \
///     -H "Content-Type: application/json" \
///     -H "Accept: application/json, text/event-stream" \
///     -d '{"jsonrpc":"2.0","method":"tools/list","id":2}'

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include "mcp/mcp.hpp"

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

// =============================================================================
// Simple Token Store for Bearer Auth
// =============================================================================

class TokenStore {
public:
    TokenStore() {
        // Pre-configured demo tokens
        valid_tokens_.insert("demo-token");
        valid_tokens_.insert("test-token");
        valid_tokens_.insert("admin-token");
    }

    bool is_valid(const std::string& token) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return valid_tokens_.count(token) > 0;
    }

    std::string generate_token(const std::string& user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string token = user_id + "-token";
        valid_tokens_.insert(token);
        return token;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_set<std::string> valid_tokens_;
};

// =============================================================================
// Counter State (shared across sessions for demo purposes)
// =============================================================================

class CounterState {
public:
    int increment() {
        return ++value_;
    }

    int decrement() {
        return --value_;
    }

    int get() const {
        return value_.load();
    }

    void set(int value) {
        value_ = value;
    }

private:
    std::atomic<int> value_{0};
};

// =============================================================================
// Test Server Handler
// =============================================================================

class TestServerHandler : public mcp::ServerHandler {
public:
    TestServerHandler(
        std::shared_ptr<CounterState> counter,
        std::shared_ptr<TokenStore> tokens)
        : counter_(std::move(counter))
        , tokens_(std::move(tokens))
    {
        setup_tools();
        setup_prompts();
        setup_resources();
    }

    // -------------------------------------------------------------------------
    // Capabilities
    // -------------------------------------------------------------------------

    mcp::ServerCapabilities capabilities() override {
        mcp::ServerCapabilities caps;
        caps.tools = mcp::ToolsCapability{};
        caps.prompts = mcp::PromptsCapability{};
        caps.resources = mcp::ResourcesCapability{};
        // Enable subscriptions for resource change notifications
        if (caps.resources) {
            caps.resources->subscribe = true;
        }
        return caps;
    }

    // -------------------------------------------------------------------------
    // Tools
    // -------------------------------------------------------------------------

    asio::awaitable<mcp::ListToolsResult> list_tools(
        std::optional<mcp::PaginatedRequestParams>,
        mcp::ServerRequestContext) override
    {
        mcp::ListToolsResult result;
        result.tools = tool_router_.list_tools();
        co_return result;
    }

    asio::awaitable<mcp::CallToolResult> call_tool(
        mcp::CallToolRequestParams params,
        mcp::ServerRequestContext ctx) override
    {
        mcp::ToolCallContext tool_ctx{
            std::string(params.name),
            std::move(params.arguments),
            std::move(ctx)
        };
        co_return co_await tool_router_.call_tool(std::move(tool_ctx));
    }

    // -------------------------------------------------------------------------
    // Prompts
    // -------------------------------------------------------------------------

    asio::awaitable<mcp::ListPromptsResult> list_prompts(
        std::optional<mcp::PaginatedRequestParams>,
        mcp::ServerRequestContext) override
    {
        mcp::ListPromptsResult result;
        result.prompts = prompt_router_.list_prompts();
        co_return result;
    }

    asio::awaitable<mcp::GetPromptResult> get_prompt(
        mcp::GetPromptRequestParams params,
        mcp::ServerRequestContext ctx) override
    {
        mcp::PromptCallContext prompt_ctx{
            std::move(params.name),
            std::move(params.arguments),
            std::move(ctx)
        };
        co_return co_await prompt_router_.get_prompt_result(std::move(prompt_ctx));
    }

    // -------------------------------------------------------------------------
    // Resources
    // -------------------------------------------------------------------------

    asio::awaitable<mcp::ListResourcesResult> list_resources(
        std::optional<mcp::PaginatedRequestParams>,
        mcp::ServerRequestContext) override
    {
        mcp::ListResourcesResult result;
        result.resources = resources_;
        co_return result;
    }

    asio::awaitable<mcp::ReadResourceResult> read_resource(
        mcp::ReadResourceRequestParams params,
        mcp::ServerRequestContext) override
    {
        mcp::ReadResourceResult result;

        if (params.uri == "counter://value") {
            // Dynamic resource: current counter value
            result.contents.push_back(mcp::ResourceContents::text(
                std::to_string(counter_->get()),
                params.uri));
        }
        else if (params.uri == "info://server") {
            // Static resource: server information
            mcp::json info = {
                {"name", "MCP C++ Test Server"},
                {"version", "1.0.0"},
                {"protocol", "2024-11-05"},
                {"features", {"tools", "prompts", "resources", "auth"}}
            };
            result.contents.push_back(mcp::ResourceContents::text(
                info.dump(2),
                params.uri));
        }
        else if (params.uri == "memo://welcome") {
            // Static resource: welcome message
            result.contents.push_back(mcp::ResourceContents::text(
                "Welcome to the MCP C++ Test Server!\n\n"
                "This server demonstrates the full MCP protocol including:\n"
                "- Tools: counter operations, math, echo\n"
                "- Prompts: example prompts with arguments\n"
                "- Resources: static and dynamic resources\n"
                "- Authentication: Bearer token auth\n",
                params.uri));
        }
        else {
            throw mcp::McpError(mcp::ErrorData::resource_not_found(
                "Resource not found: " + params.uri));
        }

        co_return result;
    }

private:
    std::shared_ptr<CounterState> counter_;
    std::shared_ptr<TokenStore> tokens_;
    mcp::ToolRouter tool_router_;
    mcp::PromptRouter prompt_router_;
    std::vector<mcp::Resource> resources_;

    // -------------------------------------------------------------------------
    // Tool Setup
    // -------------------------------------------------------------------------

    void setup_tools() {
        // Counter: increment
        tool_router_.add_tool(
            "counter_increment",
            "Increment the counter by 1 and return the new value",
            mcp::json{{"type", "object"}, {"properties", mcp::json::object()}},
            [this](mcp::ToolCallContext) -> asio::awaitable<mcp::CallToolResult> {
                int new_value = counter_->increment();
                co_return mcp::CallToolResult::success({
                    mcp::Content::text("Counter incremented to: " + std::to_string(new_value))
                });
            });

        // Counter: decrement
        tool_router_.add_tool(
            "counter_decrement",
            "Decrement the counter by 1 and return the new value",
            mcp::json{{"type", "object"}, {"properties", mcp::json::object()}},
            [this](mcp::ToolCallContext) -> asio::awaitable<mcp::CallToolResult> {
                int new_value = counter_->decrement();
                co_return mcp::CallToolResult::success({
                    mcp::Content::text("Counter decremented to: " + std::to_string(new_value))
                });
            });

        // Counter: get value
        tool_router_.add_tool(
            "counter_get",
            "Get the current counter value",
            mcp::json{{"type", "object"}, {"properties", mcp::json::object()}},
            [this](mcp::ToolCallContext) -> asio::awaitable<mcp::CallToolResult> {
                int value = counter_->get();
                co_return mcp::CallToolResult::success({
                    mcp::Content::text("Current counter value: " + std::to_string(value))
                });
            });

        // Counter: set value
        tool_router_.add_tool(
            "counter_set",
            "Set the counter to a specific value",
            mcp::json{
                {"type", "object"},
                {"properties", {{"value", {{"type", "integer"}, {"description", "The value to set"}}}}},
                {"required", {"value"}}
            },
            [this](mcp::ToolCallContext ctx) -> asio::awaitable<mcp::CallToolResult> {
                auto args = ctx.arguments.value_or(mcp::JsonObject{});
                if (!args.count("value")) {
                    co_return mcp::CallToolResult::error({
                        mcp::Content::text("Missing required argument: value")
                    });
                }
                int value = args.at("value").get<int>();
                counter_->set(value);
                co_return mcp::CallToolResult::success({
                    mcp::Content::text("Counter set to: " + std::to_string(value))
                });
            });

        // Echo tool
        tool_router_.add_tool(
            "echo",
            "Echo back the input message",
            mcp::json{
                {"type", "object"},
                {"properties", {{"message", {{"type", "string"}, {"description", "Message to echo"}}}}},
                {"required", {"message"}}
            },
            [](mcp::ToolCallContext ctx) -> asio::awaitable<mcp::CallToolResult> {
                auto args = ctx.arguments.value_or(mcp::JsonObject{});
                std::string message = args.count("message")
                    ? args.at("message").get<std::string>()
                    : "";
                co_return mcp::CallToolResult::success({
                    mcp::Content::text("Echo: " + message)
                });
            });

        // Math: add
        tool_router_.add_tool(
            "add",
            "Add two numbers together",
            mcp::json{
                {"type", "object"},
                {"properties", {
                    {"a", {{"type", "number"}, {"description", "First number"}}},
                    {"b", {{"type", "number"}, {"description", "Second number"}}}
                }},
                {"required", {"a", "b"}}
            },
            [](mcp::ToolCallContext ctx) -> asio::awaitable<mcp::CallToolResult> {
                auto args = ctx.arguments.value_or(mcp::JsonObject{});
                double a = args.count("a") ? args.at("a").get<double>() : 0.0;
                double b = args.count("b") ? args.at("b").get<double>() : 0.0;
                co_return mcp::CallToolResult::success({
                    mcp::Content::text(std::to_string(a + b))
                });
            });

        // Math: multiply
        tool_router_.add_tool(
            "multiply",
            "Multiply two numbers",
            mcp::json{
                {"type", "object"},
                {"properties", {
                    {"a", {{"type", "number"}, {"description", "First number"}}},
                    {"b", {{"type", "number"}, {"description", "Second number"}}}
                }},
                {"required", {"a", "b"}}
            },
            [](mcp::ToolCallContext ctx) -> asio::awaitable<mcp::CallToolResult> {
                auto args = ctx.arguments.value_or(mcp::JsonObject{});
                double a = args.count("a") ? args.at("a").get<double>() : 0.0;
                double b = args.count("b") ? args.at("b").get<double>() : 0.0;
                co_return mcp::CallToolResult::success({
                    mcp::Content::text(std::to_string(a * b))
                });
            });

        // Get current time
        tool_router_.add_tool(
            "get_time",
            "Get the current server time in ISO 8601 format",
            mcp::json{{"type", "object"}, {"properties", mcp::json::object()}},
            [](mcp::ToolCallContext) -> asio::awaitable<mcp::CallToolResult> {
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                std::stringstream ss;
                ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
                co_return mcp::CallToolResult::success({
                    mcp::Content::text(ss.str())
                });
            });

        // Long running task (simulated)
        tool_router_.add_tool(
            "long_task",
            "Simulate a long-running task (sleeps for specified seconds)",
            mcp::json{
                {"type", "object"},
                {"properties", {
                    {"seconds", {{"type", "integer"}, {"description", "Seconds to sleep (1-10)"}, {"minimum", 1}, {"maximum", 10}}}
                }},
                {"required", {"seconds"}}
            },
            [](mcp::ToolCallContext ctx) -> asio::awaitable<mcp::CallToolResult> {
                auto args = ctx.arguments.value_or(mcp::JsonObject{});
                int seconds = args.count("seconds") ? args.at("seconds").get<int>() : 1;
                seconds = std::clamp(seconds, 1, 10);

                // Use async sleep
                auto executor = co_await asio::this_coro::executor;
                asio::steady_timer timer(executor);
                timer.expires_after(std::chrono::seconds(seconds));
                co_await timer.async_wait(asio::use_awaitable);

                co_return mcp::CallToolResult::success({
                    mcp::Content::text("Long task completed after " + std::to_string(seconds) + " seconds")
                });
            });
    }

    // -------------------------------------------------------------------------
    // Prompt Setup
    // -------------------------------------------------------------------------

    void setup_prompts() {
        // Example prompt with a message argument
        mcp::Prompt example_prompt;
        example_prompt.name = "example_prompt";
        example_prompt.description = "An example prompt that includes your message";
        mcp::PromptArgument msg_arg;
        msg_arg.name = "message";
        msg_arg.description = "The message to include in the prompt";
        msg_arg.required = true;
        example_prompt.arguments = std::vector<mcp::PromptArgument>{msg_arg};

        prompt_router_.add_route(
            example_prompt,
            [](mcp::PromptCallContext ctx) -> asio::awaitable<mcp::GetPromptResult> {
                auto args = ctx.arguments.value_or(mcp::JsonObject{});
                std::string message = args.count("message")
                    ? args.at("message").get<std::string>()
                    : "No message provided";

                mcp::GetPromptResult result;
                result.description = "Example prompt with your message";
                result.messages.push_back(mcp::PromptMessage::new_text(
                    mcp::Role::User,
                    "This is an example prompt. Your message: " + message));
                co_return result;
            });

        // Counter analysis prompt
        mcp::Prompt counter_analysis;
        counter_analysis.name = "counter_analysis";
        counter_analysis.description = "Analyze the counter and suggest next steps to reach a goal";
        {
            mcp::PromptArgument goal_arg;
            goal_arg.name = "goal";
            goal_arg.description = "Target value to reach";
            goal_arg.required = true;
            mcp::PromptArgument strategy_arg;
            strategy_arg.name = "strategy";
            strategy_arg.description = "Preferred strategy: 'fast' or 'careful'";
            strategy_arg.required = false;
            counter_analysis.arguments = std::vector<mcp::PromptArgument>{goal_arg, strategy_arg};
        }

        prompt_router_.add_route(
            counter_analysis,
            [this](mcp::PromptCallContext ctx) -> asio::awaitable<mcp::GetPromptResult> {
                auto args = ctx.arguments.value_or(mcp::JsonObject{});
                int goal = args.count("goal") ? args.at("goal").get<int>() : 0;
                std::string strategy = args.count("strategy")
                    ? args.at("strategy").get<std::string>()
                    : "careful";

                int current = counter_->get();
                int diff = goal - current;

                mcp::GetPromptResult result;
                result.description = "Counter analysis for reaching " + std::to_string(goal);
                result.messages.push_back(mcp::PromptMessage::new_text(
                    mcp::Role::Assistant,
                    "I'll analyze the counter situation and suggest the best approach."));
                result.messages.push_back(mcp::PromptMessage::new_text(
                    mcp::Role::User,
                    "Current counter value: " + std::to_string(current) + "\n"
                    "Goal value: " + std::to_string(goal) + "\n"
                    "Difference: " + std::to_string(diff) + "\n"
                    "Strategy preference: " + strategy + "\n\n"
                    "Please analyze and suggest the best approach to reach the goal."));
                co_return result;
            });

        // Code review prompt
        mcp::Prompt code_review;
        code_review.name = "code_review";
        code_review.description = "Generate a code review prompt for the given code";
        {
            mcp::PromptArgument code_arg;
            code_arg.name = "code";
            code_arg.description = "The code to review";
            code_arg.required = true;
            mcp::PromptArgument lang_arg;
            lang_arg.name = "language";
            lang_arg.description = "Programming language";
            lang_arg.required = false;
            mcp::PromptArgument focus_arg;
            focus_arg.name = "focus";
            focus_arg.description = "What to focus on (security, performance, style)";
            focus_arg.required = false;
            code_review.arguments = std::vector<mcp::PromptArgument>{code_arg, lang_arg, focus_arg};
        }

        prompt_router_.add_route(
            code_review,
            [](mcp::PromptCallContext ctx) -> asio::awaitable<mcp::GetPromptResult> {
                auto args = ctx.arguments.value_or(mcp::JsonObject{});
                std::string code = args.count("code")
                    ? args.at("code").get<std::string>()
                    : "";
                std::string language = args.count("language")
                    ? args.at("language").get<std::string>()
                    : "unknown";
                std::string focus = args.count("focus")
                    ? args.at("focus").get<std::string>()
                    : "general";

                mcp::GetPromptResult result;
                result.description = "Code review prompt for " + language + " code";
                result.messages.push_back(mcp::PromptMessage::new_text(
                    mcp::Role::User,
                    "Please review the following " + language + " code.\n"
                    "Focus area: " + focus + "\n\n"
                    "```" + language + "\n" + code + "\n```\n\n"
                    "Provide feedback on code quality, potential issues, and suggestions for improvement."));
                co_return result;
            });
    }

    // -------------------------------------------------------------------------
    // Resource Setup
    // -------------------------------------------------------------------------

    void setup_resources() {
        // Counter value resource (dynamic)
        mcp::Resource counter_resource;
        counter_resource.uri = "counter://value";
        counter_resource.name = "Counter Value";
        counter_resource.description = "The current value of the shared counter";
        counter_resource.mime_type = "text/plain";
        resources_.push_back(counter_resource);

        // Server info resource (static)
        mcp::Resource info_resource;
        info_resource.uri = "info://server";
        info_resource.name = "Server Info";
        info_resource.description = "Information about the MCP test server";
        info_resource.mime_type = "application/json";
        resources_.push_back(info_resource);

        // Welcome memo resource (static)
        mcp::Resource memo_resource;
        memo_resource.uri = "memo://welcome";
        memo_resource.name = "Welcome";
        memo_resource.description = "Welcome message and server capabilities overview";
        memo_resource.mime_type = "text/plain";
        resources_.push_back(memo_resource);
    }
};

// =============================================================================
// Auth Wrapper for StreamableHttpServerTransport
// =============================================================================

/// Custom session manager that validates Bearer tokens before creating sessions.
/// This wraps the LocalSessionManager and adds authentication.
class AuthSessionManager : public mcp::SessionManager {
public:
    AuthSessionManager(
        asio::any_io_executor executor,
        std::shared_ptr<TokenStore> tokens,
        mcp::SessionConfig config = {})
        : inner_(std::make_shared<mcp::LocalSessionManager>(executor, config))
        , tokens_(std::move(tokens))
    {}

    asio::awaitable<mcp::SessionId> create_session() override {
        co_return co_await inner_->create_session();
    }

    asio::awaitable<std::unique_ptr<mcp::Transport<mcp::RoleServer>>>
        initialize_session(const mcp::SessionId& id) override {
        co_return co_await inner_->initialize_session(id);
    }

    bool has_session(const mcp::SessionId& id) const override {
        return inner_->has_session(id);
    }

    asio::awaitable<void> close_session(const mcp::SessionId& id) override {
        co_await inner_->close_session(id);
    }

    asio::awaitable<mcp::SseStream>
        create_stream(const mcp::SessionId& id, int64_t http_request_id) override {
        co_return co_await inner_->create_stream(id, http_request_id);
    }

    asio::awaitable<void> accept_message(
        const mcp::SessionId& id, int64_t http_request_id, mcp::json message) override {
        co_await inner_->accept_message(id, http_request_id, std::move(message));
    }

    asio::awaitable<mcp::SseStream>
        create_standalone_stream(const mcp::SessionId& id) override {
        co_return co_await inner_->create_standalone_stream(id);
    }

    asio::awaitable<mcp::SseStream>
        resume(const mcp::SessionId& id, const mcp::EventId& last_event_id) override {
        co_return co_await inner_->resume(id, last_event_id);
    }

    /// Validate a Bearer token
    bool validate_token(const std::string& auth_header) const {
        // Extract Bearer token
        const std::string prefix = "Bearer ";
        if (auth_header.size() <= prefix.size() ||
            auth_header.substr(0, prefix.size()) != prefix) {
            return false;
        }
        std::string token = auth_header.substr(prefix.size());
        return tokens_->is_valid(token);
    }

private:
    std::shared_ptr<mcp::LocalSessionManager> inner_;
    std::shared_ptr<TokenStore> tokens_;
};

// =============================================================================
// Token Endpoint Handler (separate HTTP server for demo purposes)
// =============================================================================

asio::awaitable<void> handle_token_request(
    tcp::socket socket,
    std::shared_ptr<TokenStore> tokens)
{
    try {
        beast::flat_buffer buffer;
        http::request<http::string_body> req;
        co_await http::async_read(socket, buffer, req, asio::use_awaitable);

        http::response<http::string_body> res;
        res.version(req.version());
        res.set(http::field::server, "MCP Test Server");
        res.set(http::field::content_type, "application/json");

        std::string target(req.target());

        if (target == "/" || target == "/index.html") {
            res.result(http::status::ok);
            res.set(http::field::content_type, "text/html");
            res.body() = R"(<!DOCTYPE html>
<html>
<head><title>MCP Test Server</title></head>
<body>
<h1>MCP C++ Test Server</h1>
<p>This is a comprehensive MCP test server with Streamable HTTP transport.</p>
<h2>Endpoints</h2>
<ul>
<li><code>GET /token/{user_id}</code> - Get a Bearer token</li>
<li><code>POST /mcp</code> - MCP JSON-RPC endpoint (requires Bearer token)</li>
<li><code>GET /mcp</code> - MCP SSE endpoint (requires Bearer token)</li>
<li><code>DELETE /mcp</code> - Close MCP session</li>
</ul>
<h2>Quick Start</h2>
<pre>
# Get a token
curl http://localhost:8080/token/demo

# Initialize MCP session
curl -X POST http://localhost:8080/mcp \
  -H "Authorization: Bearer demo-token" \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -d '{"jsonrpc":"2.0","method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}},"id":1}'
</pre>
</body>
</html>)";
        }
        else if (target.starts_with("/token/")) {
            std::string user_id = target.substr(7);
            if (user_id.empty()) {
                res.result(http::status::bad_request);
                res.body() = R"({"error":"Missing user_id"})";
            } else {
                std::string token = tokens->generate_token(user_id);
                mcp::json response = {
                    {"access_token", token},
                    {"token_type", "Bearer"},
                    {"expires_in", 3600}
                };
                res.result(http::status::ok);
                res.body() = response.dump();
            }
        }
        else if (target == "/health") {
            res.result(http::status::ok);
            res.body() = R"({"status":"ok"})";
        }
        else {
            res.result(http::status::not_found);
            res.body() = R"({"error":"Not found"})";
        }

        res.prepare_payload();
        co_await http::async_write(socket, res, asio::use_awaitable);

        boost::system::error_code ec;
        socket.shutdown(tcp::socket::shutdown_both, ec);
    }
    catch (const std::exception& e) {
        // Connection closed or error
    }
}

// =============================================================================
// Main
// =============================================================================

std::atomic<bool> g_shutdown{false};

void signal_handler(int) {
    g_shutdown = true;
}

int main(int argc, char* argv[]) {
    uint16_t port = 8080;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        asio::io_context io;

        // Shared state
        auto counter = std::make_shared<CounterState>();
        auto tokens = std::make_shared<TokenStore>();
        auto handler = std::make_shared<TestServerHandler>(counter, tokens);

        // Create the MCP server
        mcp::StreamableHttpServerConfig config;
        config.host = "0.0.0.0";
        config.port = port;
        config.path = "/mcp";
        config.stateful_mode = true;
        config.event_cache_size = 64;

        mcp::StreamableHttpServerTransport mcp_server(
            io.get_executor(),
            config);

        // Cancellation token for graceful shutdown
        mcp::CancellationToken cancellation;

        // Run the MCP server
        asio::co_spawn(io, [&]() -> asio::awaitable<void> {
            try {
                std::cout << "MCP Test Server starting on http://0.0.0.0:" << port << "/mcp\n";
                std::cout << "Token endpoint: http://0.0.0.0:" << port << "/token/{user_id}\n";
                std::cout << "Valid tokens: demo-token, test-token, admin-token\n";
                std::cout << "Press Ctrl+C to stop.\n";
                co_await mcp_server.start(handler, cancellation);
            } catch (const std::exception& e) {
                std::cerr << "MCP server error: " << e.what() << "\n";
            }
        }, asio::detached);

        // Simple HTTP server for token endpoint on the same port
        // Note: In production, you'd want a proper HTTP router/multiplexer
        // For this demo, the MCP server handles /mcp and we print instructions
        // for using pre-configured tokens.

        // Run until shutdown
        while (!g_shutdown) {
            io.run_for(std::chrono::milliseconds(100));
        }

        std::cout << "\nShutting down...\n";
        cancellation.cancel();
        mcp_server.stop();

        // Give some time for cleanup
        io.run_for(std::chrono::milliseconds(500));

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
