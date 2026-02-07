#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>

#include <boost/asio.hpp>

#include "mcp/transport/transport.hpp"
#include "mcp/service/cancellation_token.hpp"

namespace mcp {

namespace asio = boost::asio;

// =============================================================================
// WorkerContext — the interface exposed to the worker coroutine
// =============================================================================

/// Context passed to the worker coroutine. Provides methods to push received
/// messages to the handler and pull messages that the handler wants to send.
template <typename R>
class WorkerContext {
public:
    /// Push a message received from the external source to the handler.
    void push_received(RxJsonRpcMessage<R> msg) {
        {
            std::lock_guard<std::mutex> lock(to_handler_mutex_);
            to_handler_queue_.push(std::move(msg));
        }
        to_handler_signal_->cancel();
    }

    /// Wait for and pop the next message that the handler wants to send.
    /// Returns nullopt if the worker should stop.
    asio::awaitable<std::optional<TxJsonRpcMessage<R>>> next_outgoing() {
        while (true) {
            {
                std::lock_guard<std::mutex> lock(from_handler_mutex_);
                if (!from_handler_queue_.empty()) {
                    auto msg = std::move(from_handler_queue_.front());
                    from_handler_queue_.pop();
                    co_return std::move(msg);
                }
                if (closed_) co_return std::nullopt;
            }
            boost::system::error_code ec;
            co_await from_handler_signal_->async_wait(
                asio::redirect_error(asio::use_awaitable, ec));
            from_handler_signal_->expires_at(asio::steady_timer::time_point::max());
        }
    }

    /// Check whether the worker has been asked to stop.
    bool is_closed() const { return closed_; }

    // Constructor is public so std::make_shared can call it, but should only
    // be constructed by WorkerTransport (use WorkerTransport::create()).
    explicit WorkerContext(asio::any_io_executor executor)
        : to_handler_signal_(std::make_shared<asio::steady_timer>(executor))
        , from_handler_signal_(std::make_shared<asio::steady_timer>(executor)) {
        to_handler_signal_->expires_at(asio::steady_timer::time_point::max());
        from_handler_signal_->expires_at(asio::steady_timer::time_point::max());
    }

private:
    template <typename> friend class WorkerTransport;

    // --- to_handler: worker → handler (for receive()) ---
    std::queue<RxJsonRpcMessage<R>> to_handler_queue_;
    std::mutex to_handler_mutex_;
    std::shared_ptr<asio::steady_timer> to_handler_signal_;

    // --- from_handler: handler → worker (for send()) ---
    std::queue<TxJsonRpcMessage<R>> from_handler_queue_;
    std::mutex from_handler_mutex_;
    std::shared_ptr<asio::steady_timer> from_handler_signal_;

    bool closed_ = false;

    void close() {
        closed_ = true;
        to_handler_signal_->cancel();
        from_handler_signal_->cancel();
    }
};

// =============================================================================
// WorkerTransport
// =============================================================================

/// The type of the worker coroutine function.
template <typename R>
using WorkerFn = std::function<
    asio::awaitable<void>(std::shared_ptr<WorkerContext<R>>)>;

/// Transport that runs a user-provided coroutine in the background.
///
/// The worker coroutine communicates with the MCP handler via two async
/// queues exposed through WorkerContext. This enables complex transport
/// patterns like SSE reconnection or custom protocol adapters.
///
/// Usage:
/// ```cpp
/// auto transport = WorkerTransport<RoleClient>::create(executor,
///     [](std::shared_ptr<WorkerContext<RoleClient>> ctx) -> asio::awaitable<void> {
///         // Read from external source, push to handler:
///         ctx->push_received(msg);
///         // Get messages to send:
///         auto out = co_await ctx->next_outgoing();
///     });
/// ```
template <typename R>
class WorkerTransport : public Transport<R> {
public:
    /// Create a WorkerTransport and spawn the worker coroutine.
    static std::unique_ptr<WorkerTransport<R>> create(
        asio::any_io_executor executor,
        WorkerFn<R> worker_fn) {
        auto transport = std::unique_ptr<WorkerTransport<R>>(
            new WorkerTransport<R>(executor));

        auto ctx = transport->ctx_;
        asio::co_spawn(executor,
            [worker_fn = std::move(worker_fn), ctx]() -> asio::awaitable<void> {
                try {
                    co_await worker_fn(ctx);
                } catch (const std::exception&) {
                    // Worker coroutine failed; silently close context
                }
                ctx->close();
            },
            asio::detached);

        return transport;
    }

    ~WorkerTransport() override {
        if (ctx_) ctx_->close();
    }

    asio::awaitable<void> send(TxJsonRpcMessage<R> msg) override {
        if (ctx_->is_closed()) co_return;
        {
            std::lock_guard<std::mutex> lock(ctx_->from_handler_mutex_);
            ctx_->from_handler_queue_.push(std::move(msg));
        }
        ctx_->from_handler_signal_->cancel();
        co_return;
    }

    asio::awaitable<std::optional<RxJsonRpcMessage<R>>> receive() override {
        while (true) {
            {
                std::lock_guard<std::mutex> lock(ctx_->to_handler_mutex_);
                if (!ctx_->to_handler_queue_.empty()) {
                    auto msg = std::move(ctx_->to_handler_queue_.front());
                    ctx_->to_handler_queue_.pop();
                    co_return std::move(msg);
                }
                if (ctx_->is_closed()) co_return std::nullopt;
            }
            boost::system::error_code ec;
            co_await ctx_->to_handler_signal_->async_wait(
                asio::redirect_error(asio::use_awaitable, ec));
            ctx_->to_handler_signal_->expires_at(
                asio::steady_timer::time_point::max());
        }
    }

    asio::awaitable<void> close() override {
        ctx_->close();
        co_return;
    }

private:
    explicit WorkerTransport(asio::any_io_executor executor)
        : ctx_(std::make_shared<WorkerContext<R>>(executor)) {}

    std::shared_ptr<WorkerContext<R>> ctx_;
};

} // namespace mcp
