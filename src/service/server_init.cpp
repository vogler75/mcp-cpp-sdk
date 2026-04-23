#include "mcp/service/service.hpp"

#include <spdlog/spdlog.h>

namespace mcp {

asio::awaitable<RunningService<RoleServer>> serve_server(
    std::unique_ptr<Transport<RoleServer>> transport,
    std::shared_ptr<ServerHandler> handler,
    CancellationToken cancellation) {

    using TxMsg = TxJsonRpcMessage<RoleServer>;
    using RxMsg = RxJsonRpcMessage<RoleServer>;

    auto transport_ptr = std::shared_ptr<Transport<RoleServer>>(std::move(transport));

    // -------------------------------------------------------------------------
    // Step 1: Wait for the client's InitializeRequest
    // -------------------------------------------------------------------------
    spdlog::info("Server: waiting for initialize request...");

    auto init_msg = co_await transport_ptr->receive();
    if (!init_msg || !init_msg->is_request()) {
        throw McpError(ErrorData::internal_error(
            "Expected InitializeRequest as first message"));
    }

    auto& init_req = init_msg->as_request();
    if (!init_req.request.is<InitializeRequest>()) {
        throw McpError(ErrorData::internal_error(
            "First request must be initialize, got: " + init_req.request.method()));
    }

    auto client_params = init_req.request.get<InitializeRequest>().params;
    auto init_id = init_req.id;

    spdlog::info(
        "Server: received initialize request from client '{}' (protocol {})",
        client_params.client_info.name,
        client_params.protocol_version.value());

    // -------------------------------------------------------------------------
    // Step 2: Build InitializeResult from handler capabilities and server info
    // -------------------------------------------------------------------------
    InitializeResult result;
    result.protocol_version = ProtocolVersion::LATEST;
    result.capabilities = handler->capabilities();
    result.server_info = Implementation::from_build_env();

    // -------------------------------------------------------------------------
    // Step 3: Send the InitializeResult as a JSON-RPC response
    // -------------------------------------------------------------------------
    co_await transport_ptr->send(TxMsg::response(ServerResult(result), init_id));
    spdlog::info("Server: sent initialize result");

    // Enter the main receive loop immediately after sending InitializeResult.
    // The client's `notifications/initialized` message is handled as a regular
    // notification by the loop below. Streamable HTTP transports have no
    // ordering guarantee between POSTs, so a `tools/list` can arrive before
    // `initialized` — gating here would drop it.

    // -------------------------------------------------------------------------
    // Step 4: Create Peer<RoleServer> with a send callback through the transport
    // -------------------------------------------------------------------------
    auto peer = std::make_shared<Peer<RoleServer>>(
        [transport_ptr](TxMsg msg) -> asio::awaitable<void> {
            co_await transport_ptr->send(std::move(msg));
        });
    peer->set_peer_info(client_params);

    // -------------------------------------------------------------------------
    // Step 5: Call handler->on_initialized()
    // -------------------------------------------------------------------------
    // Fires once the server has finished its side of initialization, regardless
    // of whether the client's `initialized` notification has yet arrived (which
    // it may not, over Streamable HTTP, until some time later).
    ServerRequestContext init_ctx{peer, RequestId(int64_t{0}), cancellation.child(), {}};
    co_await handler->on_initialized(client_params, std::move(init_ctx));
    spdlog::info("Server: initialization complete");

    // -------------------------------------------------------------------------
    // Step 6: Create completion timer and RunningService
    // -------------------------------------------------------------------------
    auto executor = co_await asio::this_coro::executor;
    auto completion_timer = std::make_shared<asio::steady_timer>(executor);
    completion_timer->expires_at(asio::steady_timer::time_point::max());

    RunningService<RoleServer> service(peer, cancellation, client_params);
    service.set_completion(completion_timer);

    // -------------------------------------------------------------------------
    // Step 7: Start the main receive loop in a background coroutine
    // -------------------------------------------------------------------------
    auto handler_copy = handler;
    auto peer_copy = peer;
    auto cancel_copy = cancellation;
    auto timer_copy = completion_timer;
    auto transport_copy = transport_ptr;

    asio::co_spawn(
        executor,
        [handler_copy, peer_copy, cancel_copy, timer_copy,
         transport_copy]() -> asio::awaitable<void> {
            while (!cancel_copy.is_cancelled()) {
                auto msg = co_await transport_copy->receive();
                if (!msg) {
                    spdlog::info("Server: transport closed, ending receive loop");
                    break;
                }

                if (msg->is_request()) {
                    auto req_pair = msg->into_request();
                    if (!req_pair) continue;
                    auto& [req, id] = *req_pair;
                    ServerRequestContext ctx{
                        peer_copy, id, cancel_copy.child(), {}};

                    std::optional<ErrorData> error_to_send;
                    try {
                        auto result = co_await handler_copy->handle_request(
                            std::move(req), std::move(ctx));
                        co_await transport_copy->send(
                            TxJsonRpcMessage<RoleServer>::response(
                                std::move(result), id));
                    } catch (const McpError& e) {
                        spdlog::warn(
                            "Server: request handler error: {}", e.what());
                        error_to_send = e.error_data();
                    } catch (const std::exception& e) {
                        spdlog::error(
                            "Server: unexpected request handler error: {}",
                            e.what());
                        error_to_send = ErrorData::internal_error(e.what());
                    }
                    if (error_to_send) {
                        co_await transport_copy->send(
                            TxJsonRpcMessage<RoleServer>::error(
                                std::move(*error_to_send), id));
                    }
                } else if (msg->is_notification()) {
                    auto notif = msg->into_notification();
                    if (!notif) continue;

                    try {
                        co_await handler_copy->handle_notification(
                            std::move(*notif));
                    } catch (const std::exception& e) {
                        spdlog::warn(
                            "Server: notification handler error: {}", e.what());
                    }
                } else if (auto result_pair = msg->into_result()) {
                    auto& [result, id] = *result_pair;
                    peer_copy->complete_request(id, std::move(result));
                } else {
                    spdlog::warn(
                        "Server: received unrecognized message type, ignoring");
                }
            }

            // Mark the peer as disconnected and signal completion
            peer_copy->disconnect();
            timer_copy->cancel();
        },
        asio::detached);

    co_return service;
}

} // namespace mcp
