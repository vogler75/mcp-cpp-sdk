#include "mcp/service/peer.hpp"
#include "mcp/service/service_role.hpp"

namespace mcp {

template <typename R>
auto Peer<R>::make_response_channel(const RequestId& id)
    -> std::pair<ResponseCallback, asio::awaitable<std::variant<PeerResp, ErrorData>>> {

    using Result = std::variant<PeerResp, ErrorData>;

    // Shared state bridges the synchronous callback (invoked by complete_request)
    // with the coroutine that is waiting for the result.
    struct SharedState {
        std::mutex mutex;
        std::optional<Result> result;
        // Pointer to the timer that the awaitable is sleeping on.
        // Set by the awaitable before it starts waiting; the callback
        // cancels the timer to wake the awaitable immediately.
        asio::steady_timer* timer = nullptr;
    };

    auto state = std::make_shared<SharedState>();

    // The callback stores the result and wakes the awaitable by cancelling
    // the timer.  This is safe to call from any thread.
    ResponseCallback callback = [state](Result result) {
        std::lock_guard lock(state->mutex);
        state->result = std::move(result);
        if (state->timer) {
            state->timer->cancel();
        }
    };

    // Register the callback in the pending map so that complete_request()
    // can find it when a response arrives.
    {
        std::lock_guard lock(mutex_);
        pending_[id.to_string()] = callback;
    }

    // Build the awaitable.  The shared state pointer is passed as a parameter
    // to the coroutine (not captured in a lambda) so its lifetime is guaranteed
    // to extend across suspension points.
    auto awaitable = [](std::shared_ptr<SharedState> st) -> asio::awaitable<Result> {
        // Check if the result was already delivered before we even started
        // waiting (e.g. the callback fired synchronously).
        {
            std::lock_guard lock(st->mutex);
            if (st->result.has_value()) {
                co_return std::move(*st->result);
            }
        }

        auto executor = co_await asio::this_coro::executor;
        asio::steady_timer timer(executor);

        // Publish the timer pointer so the callback can cancel it.
        {
            std::lock_guard lock(st->mutex);
            // Double-check: the result may have arrived between our first
            // check and the timer creation.
            if (st->result.has_value()) {
                co_return std::move(*st->result);
            }
            st->timer = &timer;
        }

        // Wait in a loop.  Each iteration sets a generous expiry so we are
        // not busy-spinning, but the callback will cancel the timer for an
        // immediate wake-up in the common case.
        while (true) {
            timer.expires_after(std::chrono::seconds(30));
            boost::system::error_code ec;
            co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));

            std::lock_guard lock(st->mutex);
            if (st->result.has_value()) {
                st->timer = nullptr;
                co_return std::move(*st->result);
            }
            // Timer expired or was cancelled spuriously -- loop and wait again.
        }
    }(state);

    return {std::move(callback), std::move(awaitable)};
}

// Explicit template instantiations for the two roles.
template class Peer<RoleClient>;
template class Peer<RoleServer>;

} // namespace mcp
