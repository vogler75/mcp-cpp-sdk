#pragma once

#include <memory>

#include <boost/asio.hpp>

#include "mcp/service/cancellation_token.hpp"
#include "mcp/service/peer.hpp"
#include "mcp/service/service_role.hpp"

namespace mcp {

namespace asio = boost::asio;

// Forward declaration
enum class QuitReason;

/// Represents a running MCP service.
///
/// Provides access to the peer (for sending requests/notifications)
/// and lifecycle control (close, cancel, wait).
template <typename R>
class RunningService {
public:
    RunningService() = default;

    RunningService(
        std::shared_ptr<Peer<R>> peer,
        CancellationToken cancellation,
        typename R::PeerInfo peer_info)
        : peer_(std::move(peer))
        , cancellation_(std::move(cancellation))
        , peer_info_(std::move(peer_info)) {}

    /// Get the peer for sending requests/notifications to the remote side.
    std::shared_ptr<Peer<R>> peer() const { return peer_; }

    /// Get the remote side's info (received during initialization).
    const typename R::PeerInfo& peer_info() const { return peer_info_; }

    /// Request graceful shutdown.
    void close() {
        cancellation_.cancel();
    }

    /// Cancel the service immediately.
    void cancel() {
        cancellation_.cancel();
    }

    /// Check if the service has been closed.
    bool is_closed() const {
        return cancellation_.is_cancelled();
    }

    /// Get the cancellation token.
    CancellationToken cancellation_token() const { return cancellation_; }

    /// Wait for the service to complete.
    /// This is set by the service layer after the main loop starts.
    asio::awaitable<QuitReason> wait();

    // Internal: set the completion future
    void set_completion(std::shared_ptr<asio::steady_timer> timer) {
        completion_timer_ = std::move(timer);
    }

private:
    std::shared_ptr<Peer<R>> peer_;
    CancellationToken cancellation_;
    typename R::PeerInfo peer_info_{};
    std::shared_ptr<asio::steady_timer> completion_timer_;
};

} // namespace mcp
