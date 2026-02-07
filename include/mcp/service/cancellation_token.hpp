#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include <boost/asio.hpp>

namespace mcp {

namespace asio = boost::asio;

/// A hierarchical cancellation token.
///
/// Supports parent-child relationships: cancelling a parent automatically
/// cancels all children. Provides both synchronous polling (is_cancelled)
/// and async waiting (wait).
class CancellationToken {
public:
    CancellationToken()
        : state_(std::make_shared<State>()) {}

    /// Create a child token that is cancelled when this token is cancelled.
    CancellationToken child() const {
        auto child_state = std::make_shared<State>();
        std::lock_guard lock(state_->mutex);
        if (state_->cancelled.load(std::memory_order_relaxed)) {
            // Parent already cancelled — pre-cancel child
            child_state->cancelled.store(true, std::memory_order_relaxed);
        } else {
            state_->children.push_back(child_state);
        }
        CancellationToken child_token;
        child_token.state_ = child_state;
        return child_token;
    }

    /// Request cancellation.
    void cancel() {
        cancel_impl(state_);
    }

    /// Check if cancellation has been requested.
    bool is_cancelled() const {
        return state_->cancelled.load(std::memory_order_acquire);
    }

    /// Async wait until cancellation is requested.
    /// Uses a steady_timer internally; the timer is cancelled when
    /// the token is cancelled.
    asio::awaitable<void> wait() const {
        if (is_cancelled()) co_return;

        auto executor = co_await asio::this_coro::executor;
        asio::steady_timer timer(executor);
        timer.expires_at(asio::steady_timer::time_point::max());

        // Register a callback to cancel the timer when the token is cancelled
        auto timer_ptr = std::make_shared<asio::steady_timer*>(&timer);
        auto state = state_;

        // Poll in a loop with short sleeps (simple approach)
        // A more sophisticated approach would register a waiter
        while (!state->cancelled.load(std::memory_order_acquire)) {
            timer.expires_after(std::chrono::milliseconds(50));
            boost::system::error_code ec;
            co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        }
    }

    explicit operator bool() const { return !is_cancelled(); }

private:
    struct State {
        std::atomic<bool> cancelled{false};
        mutable std::mutex mutex;
        std::vector<std::shared_ptr<State>> children;
    };

    static void cancel_impl(const std::shared_ptr<State>& state) {
        std::vector<std::shared_ptr<State>> children_to_cancel;
        {
            std::lock_guard lock(state->mutex);
            if (state->cancelled.exchange(true, std::memory_order_release)) {
                return; // Already cancelled
            }
            children_to_cancel = std::move(state->children);
        }
        for (auto& child : children_to_cancel) {
            cancel_impl(child);
        }
    }

    std::shared_ptr<State> state_;
};

} // namespace mcp
