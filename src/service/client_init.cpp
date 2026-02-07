#include "mcp/service/service.hpp"

#include <spdlog/spdlog.h>

namespace mcp {

asio::awaitable<RunningService<RoleClient>> serve_client(
    std::unique_ptr<Transport<RoleClient>> transport,
    std::shared_ptr<ClientHandler> handler,
    InitializeRequestParams client_info,
    CancellationToken cancellation) {

    using TxMsg = TxJsonRpcMessage<RoleClient>;
    using RxMsg = RxJsonRpcMessage<RoleClient>;

    auto transport_ptr = std::shared_ptr<Transport<RoleClient>>(std::move(transport));

    // -------------------------------------------------------------------------
    // Step 1: Create Peer<RoleClient> with a send callback through the transport
    // -------------------------------------------------------------------------
    auto peer = std::make_shared<Peer<RoleClient>>(
        [transport_ptr](TxMsg msg) -> asio::awaitable<void> {
            co_await transport_ptr->send(std::move(msg));
        });

    // -------------------------------------------------------------------------
    // Step 2: Send InitializeRequest with client_info
    // -------------------------------------------------------------------------
    spdlog::info(
        "Client: sending initialize request (client '{}', protocol {})",
        client_info.client_info.name,
        client_info.protocol_version.value());

    // Merge handler capabilities into the client_info
    client_info.capabilities = handler->capabilities();

    InitializeRequest init_req;
    init_req.params = client_info;

    RequestId init_id(int64_t{0});
    co_await transport_ptr->send(
        TxMsg::request(ClientRequest(init_req), init_id));

    // -------------------------------------------------------------------------
    // Step 3: Wait for the server's InitializeResult response
    // -------------------------------------------------------------------------
    spdlog::info("Client: waiting for initialize response...");

    auto resp_msg = co_await transport_ptr->receive();
    if (!resp_msg) {
        throw McpError(ErrorData::internal_error(
            "Transport closed before receiving InitializeResult"));
    }

    InitializeResult server_result;
    if (resp_msg->is_response()) {
        auto resp_pair = resp_msg->into_response();
        if (!resp_pair) {
            throw McpError(ErrorData::internal_error(
                "Failed to extract InitializeResult from response"));
        }
        auto& [result, resp_id] = *resp_pair;
        if (!result.is<InitializeResult>()) {
            throw McpError(ErrorData::internal_error(
                "Response to initialize request is not an InitializeResult"));
        }
        server_result = result.get<InitializeResult>();
    } else if (resp_msg->is_error()) {
        auto& err = resp_msg->as_error();
        throw McpError(err.error);
    } else {
        throw McpError(ErrorData::internal_error(
            "Expected response to InitializeRequest, "
            "got unexpected message type"));
    }

    spdlog::info(
        "Client: received initialize result from server '{}' (protocol {})",
        server_result.server_info.name,
        server_result.protocol_version.value());

    peer->set_peer_info(server_result);

    // -------------------------------------------------------------------------
    // Step 4: Send InitializedNotification
    // -------------------------------------------------------------------------
    co_await transport_ptr->send(
        TxMsg::notification(ClientNotification(InitializedNotification{})));
    spdlog::info("Client: sent initialized notification");

    // -------------------------------------------------------------------------
    // Step 5: Call handler->on_initialized()
    // -------------------------------------------------------------------------
    ClientRequestContext init_ctx{
        peer, RequestId(int64_t{0}), cancellation.child(), {}};
    co_await handler->on_initialized(server_result, std::move(init_ctx));
    spdlog::info("Client: initialization complete");

    // -------------------------------------------------------------------------
    // Step 6: Create completion timer and RunningService
    // -------------------------------------------------------------------------
    auto executor = co_await asio::this_coro::executor;
    auto completion_timer = std::make_shared<asio::steady_timer>(executor);
    completion_timer->expires_at(asio::steady_timer::time_point::max());

    RunningService<RoleClient> service(peer, cancellation, server_result);
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
                    spdlog::info("Client: transport closed, ending receive loop");
                    break;
                }

                if (msg->is_request()) {
                    auto req_pair = msg->into_request();
                    if (!req_pair) continue;
                    auto& [req, id] = *req_pair;
                    ClientRequestContext ctx{
                        peer_copy, id, cancel_copy.child(), {}};

                    std::optional<ErrorData> error_to_send;
                    try {
                        auto result = co_await handler_copy->handle_request(
                            std::move(req), std::move(ctx));
                        co_await transport_copy->send(
                            TxJsonRpcMessage<RoleClient>::response(
                                std::move(result), id));
                    } catch (const McpError& e) {
                        spdlog::warn(
                            "Client: request handler error: {}", e.what());
                        error_to_send = e.error_data();
                    } catch (const std::exception& e) {
                        spdlog::error(
                            "Client: unexpected request handler error: {}",
                            e.what());
                        error_to_send = ErrorData::internal_error(e.what());
                    }
                    if (error_to_send) {
                        co_await transport_copy->send(
                            TxJsonRpcMessage<RoleClient>::error(
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
                            "Client: notification handler error: {}", e.what());
                    }
                } else if (auto result_pair = msg->into_result()) {
                    auto& [result, id] = *result_pair;
                    peer_copy->complete_request(id, std::move(result));
                } else {
                    spdlog::warn(
                        "Client: received unrecognized message type, ignoring");
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
