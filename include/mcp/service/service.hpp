#pragma once

#include <memory>

#include <boost/asio.hpp>

#include "mcp/handler/client_handler.hpp"
#include "mcp/handler/server_handler.hpp"
#include "mcp/service/cancellation_token.hpp"
#include "mcp/service/peer.hpp"
#include "mcp/service/running_service.hpp"
#include "mcp/service/service_role.hpp"
#include "mcp/transport/transport.hpp"

namespace mcp {

namespace asio = boost::asio;

/// The reason a service stopped running.
enum class QuitReason {
    /// The transport was closed normally
    Closed,
    /// An error occurred
    Error,
    /// Cancellation was requested
    Cancelled,
};

/// Start a server service: perform initialization handshake, then
/// run the main loop dispatching requests to the handler.
///
/// Returns a RunningService that can be used to close or wait on the service.
asio::awaitable<RunningService<RoleServer>> serve_server(
    std::unique_ptr<Transport<RoleServer>> transport,
    std::shared_ptr<ServerHandler> handler,
    CancellationToken cancellation = {});

/// Start a client service: perform initialization handshake, then
/// run the main loop dispatching requests to the handler.
///
/// Returns a RunningService that can be used to close or wait on the service.
asio::awaitable<RunningService<RoleClient>> serve_client(
    std::unique_ptr<Transport<RoleClient>> transport,
    std::shared_ptr<ClientHandler> handler,
    InitializeRequestParams client_info = {},
    CancellationToken cancellation = {});

} // namespace mcp
