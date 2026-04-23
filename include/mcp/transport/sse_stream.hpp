#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <queue>

#include <boost/asio.hpp>

#include "mcp/transport/sse_message.hpp"

namespace mcp {

namespace asio = boost::asio;

// =============================================================================
// SseStream / SseStreamSender — async channel for SSE messages
// =============================================================================

/// Internal shared state for the SSE stream channel.
struct SseStreamState {
    std::queue<ServerSseMessage> queue;
    std::mutex mutex;
    std::shared_ptr<asio::steady_timer> signal;
    bool closed = false;

    explicit SseStreamState(asio::any_io_executor executor)
        : signal(std::make_shared<asio::steady_timer>(executor)) {
        signal->expires_at(asio::steady_timer::time_point::max());
    }
};

/// Sending end of an SSE stream channel.
/// Thread-safe: can be called from any thread.
class SseStreamSender {
public:
    explicit SseStreamSender(std::shared_ptr<SseStreamState> state)
        : state_(std::move(state)) {}

    /// Send an SSE message to the stream.
    void send(ServerSseMessage msg) {
        if (!state_) return;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (state_->closed) return;
            state_->queue.push(std::move(msg));
        }
        state_->signal->cancel();
    }

    /// Close the sending end of the stream.
    void close() {
        if (!state_) return;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->closed = true;
        }
        state_->signal->cancel();
    }

    bool is_closed() const {
        if (!state_) return true;
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->closed;
    }

private:
    std::shared_ptr<SseStreamState> state_;
};

/// Receiving end of an SSE stream channel.
/// Must be used from within a coroutine on the associated executor.
class SseStream {
public:
    explicit SseStream(std::shared_ptr<SseStreamState> state)
        : state_(std::move(state)) {}

    /// Get the next SSE message. Returns nullopt when the stream is closed
    /// and all buffered messages have been consumed.
    asio::awaitable<std::optional<ServerSseMessage>> next() {
        while (true) {
            {
                std::lock_guard<std::mutex> lock(state_->mutex);
                if (!state_->queue.empty()) {
                    auto msg = std::move(state_->queue.front());
                    state_->queue.pop();
                    co_return std::move(msg);
                }
                if (state_->closed) {
                    co_return std::nullopt;
                }
            }
            boost::system::error_code ec;
            co_await state_->signal->async_wait(
                asio::redirect_error(asio::use_awaitable, ec));
            state_->signal->expires_at(asio::steady_timer::time_point::max());
        }
    }

    /// Result of a timed poll: either a message, stream-closed, or timeout.
    struct TimedResult {
        std::optional<ServerSseMessage> message;
        bool timed_out = false;  // true if no message within the timeout

        /// True if the stream is closed and no more messages are coming.
        bool closed() const { return !message && !timed_out; }
    };

    /// Get the next SSE message with a timeout. Returns:
    /// - TimedResult{msg, false} if a message is available
    /// - TimedResult{nullopt, true} if the timeout expired
    /// - TimedResult{nullopt, false} if the stream is closed
    asio::awaitable<TimedResult> next_for(std::chrono::steady_clock::duration timeout) {
        // Check for already-queued messages first
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (!state_->queue.empty()) {
                auto msg = std::move(state_->queue.front());
                state_->queue.pop();
                co_return TimedResult{std::move(msg), false};
            }
            if (state_->closed) {
                co_return TimedResult{std::nullopt, false};
            }
        }

        // Wait with timeout
        state_->signal->expires_after(timeout);
        boost::system::error_code ec;
        co_await state_->signal->async_wait(
            asio::redirect_error(asio::use_awaitable, ec));

        // Check again after waking up
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (!state_->queue.empty()) {
                auto msg = std::move(state_->queue.front());
                state_->queue.pop();
                // Reset timer to max for next call
                state_->signal->expires_at(asio::steady_timer::time_point::max());
                co_return TimedResult{std::move(msg), false};
            }
            if (state_->closed) {
                co_return TimedResult{std::nullopt, false};
            }
        }

        // Timer expired without message or close -> timeout
        state_->signal->expires_at(asio::steady_timer::time_point::max());
        co_return TimedResult{std::nullopt, true};
    }

    /// Close the receiving end of the stream.
    void close() {
        if (!state_) return;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->closed = true;
        }
        state_->signal->cancel();
    }

private:
    std::shared_ptr<SseStreamState> state_;
};

/// Create a paired SSE stream sender and receiver.
inline std::pair<SseStreamSender, SseStream> make_sse_stream(
    asio::any_io_executor executor) {
    auto state = std::make_shared<SseStreamState>(std::move(executor));
    return {SseStreamSender(state), SseStream(state)};
}

} // namespace mcp
