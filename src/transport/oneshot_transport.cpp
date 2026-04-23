#ifdef MCP_HTTP_TRANSPORT

#include "mcp/transport/oneshot_transport.hpp"

namespace mcp {

OneshotTransport::OneshotTransport(
    asio::any_io_executor executor,
    RxJsonRpcMessage<RoleServer> initial_message)
    : executor_(std::move(executor))
    , initial_(std::move(initial_message))
    , response_signal_(std::make_shared<asio::steady_timer>(executor_)) {
    response_signal_->expires_at(asio::steady_timer::time_point::max());
}

asio::awaitable<void> OneshotTransport::send(TxJsonRpcMessage<RoleServer> msg) {
    if (closed_) co_return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        responses_.push_back(std::move(msg));
    }
    response_signal_->cancel();
    co_return;
}

asio::awaitable<std::optional<RxJsonRpcMessage<RoleServer>>>
OneshotTransport::receive() {
    if (closed_) co_return std::nullopt;

    if (!delivered_ && initial_.has_value()) {
        delivered_ = true;
        co_return std::move(*initial_);
    }

    // After delivering the initial message, block until close() is called.
    // The service loop will keep calling receive() to check for more messages.
    // We return nullopt to signal that we're done.
    co_return std::nullopt;
}

asio::awaitable<void> OneshotTransport::close() {
    closed_ = true;
    response_signal_->cancel();
    co_return;
}

std::vector<TxJsonRpcMessage<RoleServer>> OneshotTransport::take_responses() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(responses_);
}

bool OneshotTransport::has_responses() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !responses_.empty();
}

} // namespace mcp

#endif // MCP_HTTP_TRANSPORT
