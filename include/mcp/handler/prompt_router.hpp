#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>

#include "mcp/handler/server_handler.hpp"
#include "mcp/model/prompt.hpp"
#include "mcp/model/unions.hpp"

namespace mcp {

namespace asio = boost::asio;

/// Context passed to prompt handler functions.
struct PromptCallContext {
    /// The prompt name
    std::string name;
    /// The arguments passed
    std::optional<JsonObject> arguments;
    /// The server request context
    ServerRequestContext server_ctx;
};

/// A single prompt route: metadata + handler function.
struct PromptRoute {
    /// The prompt metadata
    Prompt prompt;

    /// The async handler function
    using Handler = std::function<asio::awaitable<GetPromptResult>(PromptCallContext)>;
    Handler handler;
};

/// Routes prompt requests by name to registered handler functions.
class PromptRouter {
public:
    PromptRouter() = default;

    /// Add a prompt route.
    void add_route(Prompt prompt, PromptRoute::Handler handler) {
        std::string name = prompt.name;
        routes_.emplace(name, PromptRoute{std::move(prompt), std::move(handler)});
    }

    /// List all registered prompts.
    std::vector<Prompt> list_prompts() const {
        std::vector<Prompt> prompts;
        prompts.reserve(routes_.size());
        for (const auto& [name, route] : routes_) {
            prompts.push_back(route.prompt);
        }
        return prompts;
    }

    /// Get a prompt by name.
    const Prompt* get_prompt(const std::string& name) const {
        auto it = routes_.find(name);
        if (it == routes_.end()) return nullptr;
        return &it->second.prompt;
    }

    /// Handle a get_prompt request by name.
    asio::awaitable<GetPromptResult> get_prompt_result(PromptCallContext ctx) {
        auto it = routes_.find(ctx.name);
        if (it == routes_.end()) {
            throw McpError(ErrorData::resource_not_found("Prompt not found: " + ctx.name));
        }
        co_return co_await it->second.handler(std::move(ctx));
    }

    bool has_prompt(const std::string& name) const {
        return routes_.count(name) > 0;
    }

    size_t size() const { return routes_.size(); }
    bool empty() const { return routes_.empty(); }

private:
    std::unordered_map<std::string, PromptRoute> routes_;
};

/// A ServerHandler that uses a PromptRouter for prompt dispatch.
class PromptRouterHandler : public ServerHandler {
public:
    ServerCapabilities capabilities() override {
        auto caps = ServerCapabilities{};
        if (!router_.empty()) {
            caps.prompts = PromptsCapability{};
        }
        return caps;
    }

    asio::awaitable<ListPromptsResult> list_prompts(
        std::optional<PaginatedRequestParams>,
        ServerRequestContext) override {
        ListPromptsResult result;
        result.prompts = router_.list_prompts();
        co_return result;
    }

    asio::awaitable<GetPromptResult> get_prompt(
        GetPromptRequestParams params,
        ServerRequestContext ctx) override {
        PromptCallContext prompt_ctx{
            std::move(params.name),
            std::move(params.arguments),
            std::move(ctx)
        };
        co_return co_await router_.get_prompt_result(std::move(prompt_ctx));
    }

protected:
    PromptRouter router_;
};

} // namespace mcp
