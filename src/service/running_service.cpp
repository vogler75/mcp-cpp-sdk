#include "mcp/service/running_service.hpp"
#include "mcp/service/service.hpp"
#include "mcp/service/service_role.hpp"

namespace mcp {

template <typename R>
asio::awaitable<QuitReason> RunningService<R>::wait() {
    if (!completion_timer_) {
        co_return QuitReason::Error;
    }

    boost::system::error_code ec;
    co_await completion_timer_->async_wait(
        asio::redirect_error(asio::use_awaitable, ec));

    if (cancellation_.is_cancelled()) {
        co_return QuitReason::Cancelled;
    }

    co_return QuitReason::Closed;
}

// Explicit template instantiations for the two roles.
template class RunningService<RoleClient>;
template class RunningService<RoleServer>;

} // namespace mcp
