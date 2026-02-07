#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <boost/asio.hpp>

#include "mcp/model/jsonrpc.hpp"
#include "mcp/model/unions.hpp"
#include "mcp/service/service_role.hpp"

namespace mcp {

namespace asio = boost::asio;

/// The JSON-RPC message type transmitted by a role R (also defined in transport.hpp)
template <typename R>
using TxJsonRpcMessage = JsonRpcMessage<
    typename R::Req,
    typename R::Resp,
    typename R::Not>;

/// Peer provides the interface to communicate with the remote side.
///
/// For a server, the peer is the client.
/// For a client, the peer is the server.
///
/// Peer handles request ID generation, response tracking, and
/// sending requests/notifications.
template <typename R>
class Peer : public std::enable_shared_from_this<Peer<R>> {
public:
    using TxMsg = TxJsonRpcMessage<R>;
    using PeerReq = typename R::PeerReq;
    using PeerResp = typename R::PeerResp;
    using PeerNot = typename R::PeerNot;

    /// Callback type for sending messages through the transport
    using SendCallback = std::function<asio::awaitable<void>(TxMsg)>;

    explicit Peer(SendCallback send_cb)
        : send_cb_(std::move(send_cb)) {}

    /// Send a request and wait for a response.
    /// Returns the result or error.
    template <typename Req>
    asio::awaitable<std::variant<PeerResp, ErrorData>> send_request(Req request) {
        auto id = next_id();
        auto [promise, future] = make_response_channel(id);

        TxMsg msg = TxMsg::request(
            typename R::Req(std::move(request)),
            id);
        co_await send_cb_(std::move(msg));

        // Wait for the response
        co_return co_await std::move(future);
    }

    /// Send a notification (fire-and-forget).
    template <typename Not>
    asio::awaitable<void> send_notification(Not notification) {
        TxMsg msg = TxMsg::notification(
            typename R::Not(std::move(notification)));
        co_await send_cb_(std::move(msg));
    }

    /// Complete a pending request with a response.
    /// Called by the service layer when a response is received.
    void complete_request(RequestId id, std::variant<PeerResp, ErrorData> result) {
        std::lock_guard lock(mutex_);
        auto it = pending_.find(id.to_string());
        if (it != pending_.end()) {
            it->second(std::move(result));
            pending_.erase(it);
        }
    }

    /// Check if the peer is still connected.
    bool is_connected() const {
        return connected_.load(std::memory_order_acquire);
    }

    /// Mark as disconnected (called when transport closes).
    void disconnect() {
        connected_.store(false, std::memory_order_release);
        // Cancel all pending requests
        std::lock_guard lock(mutex_);
        for (auto& [id, callback] : pending_) {
            callback(ErrorData::internal_error("Peer disconnected"));
        }
        pending_.clear();
    }

    /// Get the peer's info (set after initialization).
    const typename R::PeerInfo& peer_info() const { return peer_info_; }
    void set_peer_info(typename R::PeerInfo info) { peer_info_ = std::move(info); }

private:
    RequestId next_id() {
        return RequestId(counter_.fetch_add(1, std::memory_order_relaxed));
    }

    using ResponseCallback = std::function<void(std::variant<PeerResp, ErrorData>)>;

    struct ResponseChannel {
        ResponseCallback callback;
        asio::awaitable<std::variant<PeerResp, ErrorData>> future;
    };

    std::pair<ResponseCallback, asio::awaitable<std::variant<PeerResp, ErrorData>>>
    make_response_channel(const RequestId& id);

    SendCallback send_cb_;
    std::atomic<int64_t> counter_{1};
    std::atomic<bool> connected_{true};
    std::mutex mutex_;
    std::unordered_map<std::string, ResponseCallback> pending_;
    typename R::PeerInfo peer_info_{};
};

} // namespace mcp
