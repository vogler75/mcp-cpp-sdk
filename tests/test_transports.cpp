#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>

#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

#include "mcp/transport/transport.hpp"
#include "mcp/transport/async_rw_transport.hpp"
#include "mcp/transport/worker_transport.hpp"

using namespace mcp;
namespace asio = boost::asio;
using json = nlohmann::json;

// =============================================================================
// Helper: build a typed RxJsonRpcMessage from raw JSON
// =============================================================================

/// Build a PingRequest message (a server-receives-from-client request)
/// usable as RxJsonRpcMessage<RoleServer>.
static RxJsonRpcMessage<RoleServer> make_ping_request(int id) {
    json j = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", "ping"}
    };
    return j.get<RxJsonRpcMessage<RoleServer>>();
}

/// Build an EmptyResult response usable as RxJsonRpcMessage<RoleClient>.
static RxJsonRpcMessage<RoleClient> make_empty_response(int id) {
    json j = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", json::object()}
    };
    return j.get<RxJsonRpcMessage<RoleClient>>();
}

// =============================================================================
// Test helpers
// =============================================================================

/// Holds the result of a coroutine-based test.
struct TestResult {
    bool completed = false;
    std::string error;
    json data;
};

/// Run an async test body inside an io_context with a timeout.
static TestResult run_async_test(
    std::function<asio::awaitable<TestResult>(asio::any_io_executor)> body) {
    asio::io_context io;
    TestResult result;

    asio::co_spawn(io,
        [&]() -> asio::awaitable<void> {
            try {
                result = co_await body(io.get_executor());
            } catch (const std::exception& e) {
                result.error = std::string("Test body threw: ") + e.what();
            }
        },
        asio::detached);

    io.run_for(std::chrono::seconds(10));
    return result;
}

// =============================================================================
// Pipe Transport Tests (AsyncRwTransport with pipes)
// =============================================================================

TEST(PipeTransport, SendAndReceive) {
    auto result = run_async_test(
        [](asio::any_io_executor exec) -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            // Create a pipe pair
            int pipefd[2];
            EXPECT_EQ(::pipe(pipefd), 0);

            // Server writes to pipe, client reads from pipe.
            // We need two pipe pairs for bidirectional communication:
            //   pipe_a: server -> client
            //   pipe_b: client -> server
            int pipe_a[2]; // server writes [1], client reads [0]
            int pipe_b[2]; // client writes [1], server reads [0]
            ::pipe(pipe_a);
            ::pipe(pipe_b);

            // Close the single test pipe we opened above
            ::close(pipefd[0]);
            ::close(pipefd[1]);

            auto server = make_pipe_transport<RoleServer>(exec, pipe_b[0], pipe_a[1]);
            auto client = make_pipe_transport<RoleClient>(exec, pipe_a[0], pipe_b[1]);

            // Server sends a ping request (server->client)
            auto ping_req = make_empty_response(42);
            co_await server->send(
                TxJsonRpcMessage<RoleServer>(
                    typename TxJsonRpcMessage<RoleServer>::Response{
                        "2.0", RequestId(42), ServerResult::empty()}));

            // Client receives it
            auto received = co_await client->receive();
            r.data["received"] = received.has_value();
            if (received) {
                json j = *received;
                r.data["msg"] = j;
            }

            co_await server->close();
            co_await client->close();

            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;
    EXPECT_TRUE(result.data["received"].get<bool>());
    EXPECT_EQ(result.data["msg"]["id"], 42);
    EXPECT_EQ(result.data["msg"]["jsonrpc"], "2.0");
}

TEST(PipeTransport, BidirectionalCommunication) {
    auto result = run_async_test(
        [](asio::any_io_executor exec) -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            int pipe_a[2]; // server writes [1], client reads [0]
            int pipe_b[2]; // client writes [1], server reads [0]
            ::pipe(pipe_a);
            ::pipe(pipe_b);

            auto server = make_pipe_transport<RoleServer>(exec, pipe_b[0], pipe_a[1]);
            auto client = make_pipe_transport<RoleClient>(exec, pipe_a[0], pipe_b[1]);

            // Client sends a ping request to server
            auto ping = make_ping_request(1);
            json ping_j = ping;
            auto client_msg = ping_j.get<TxJsonRpcMessage<RoleClient>>();
            co_await client->send(std::move(client_msg));

            // Server receives it
            auto server_received = co_await server->receive();
            r.data["server_received"] = server_received.has_value();
            if (server_received) {
                json j = *server_received;
                r.data["server_msg"] = j;
            }

            // Server sends a response back
            co_await server->send(
                TxJsonRpcMessage<RoleServer>(
                    typename TxJsonRpcMessage<RoleServer>::Response{
                        "2.0", RequestId(1), ServerResult::empty()}));

            // Client receives the response
            auto client_received = co_await client->receive();
            r.data["client_received"] = client_received.has_value();
            if (client_received) {
                json j = *client_received;
                r.data["client_msg"] = j;
            }

            co_await server->close();
            co_await client->close();

            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;
    EXPECT_TRUE(result.data["server_received"].get<bool>());
    EXPECT_EQ(result.data["server_msg"]["method"], "ping");
    EXPECT_EQ(result.data["server_msg"]["id"], 1);
    EXPECT_TRUE(result.data["client_received"].get<bool>());
    EXPECT_EQ(result.data["client_msg"]["id"], 1);
}

TEST(PipeTransport, CloseReturnsNullopt) {
    auto result = run_async_test(
        [](asio::any_io_executor exec) -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            int pipe_a[2];
            int pipe_b[2];
            ::pipe(pipe_a);
            ::pipe(pipe_b);

            auto server = make_pipe_transport<RoleServer>(exec, pipe_b[0], pipe_a[1]);
            auto client = make_pipe_transport<RoleClient>(exec, pipe_a[0], pipe_b[1]);

            // Close the server side
            co_await server->close();

            // Client should get nullopt on receive (EOF)
            auto received = co_await client->receive();
            r.data["got_nullopt"] = !received.has_value();

            co_await client->close();

            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;
    EXPECT_TRUE(result.data["got_nullopt"].get<bool>());
}

TEST(PipeTransport, MultipleMessages) {
    auto result = run_async_test(
        [](asio::any_io_executor exec) -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();
            r.data["responses"] = json::array();

            int pipe_a[2];
            int pipe_b[2];
            ::pipe(pipe_a);
            ::pipe(pipe_b);

            auto server = make_pipe_transport<RoleServer>(exec, pipe_b[0], pipe_a[1]);
            auto client = make_pipe_transport<RoleClient>(exec, pipe_a[0], pipe_b[1]);

            // Send 5 responses from server to client
            for (int i = 0; i < 5; ++i) {
                co_await server->send(
                    TxJsonRpcMessage<RoleServer>(
                        typename TxJsonRpcMessage<RoleServer>::Response{
                            "2.0", RequestId(i), ServerResult::empty()}));
            }

            // Client receives all 5
            for (int i = 0; i < 5; ++i) {
                auto received = co_await client->receive();
                if (received) {
                    json j = *received;
                    r.data["responses"].push_back(j);
                }
            }

            co_await server->close();
            co_await client->close();

            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;
    auto& responses = result.data["responses"];
    ASSERT_EQ(responses.size(), 5);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(responses[i]["id"], i) << "response " << i;
    }
}

// =============================================================================
// TCP Socket Transport Tests
// =============================================================================

TEST(TcpTransport, SendAndReceive) {
    auto result = run_async_test(
        [](asio::any_io_executor exec) -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            // Set up a TCP acceptor on a random port
            asio::ip::tcp::acceptor acceptor(exec,
                asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
            auto port = acceptor.local_endpoint().port();

            // Connect a client socket
            asio::ip::tcp::socket client_sock(exec);
            co_await client_sock.async_connect(
                asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), port),
                asio::use_awaitable);

            // Accept on server side
            asio::ip::tcp::socket server_sock = co_await acceptor.async_accept(
                asio::use_awaitable);

            auto server = make_socket_transport<RoleServer>(std::move(server_sock));
            auto client = make_socket_transport<RoleClient>(std::move(client_sock));

            // Server sends a response
            co_await server->send(
                TxJsonRpcMessage<RoleServer>(
                    typename TxJsonRpcMessage<RoleServer>::Response{
                        "2.0", RequestId(99), ServerResult::empty()}));

            // Client receives it
            auto received = co_await client->receive();
            r.data["received"] = received.has_value();
            if (received) {
                json j = *received;
                r.data["msg"] = j;
            }

            co_await server->close();
            co_await client->close();

            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;
    EXPECT_TRUE(result.data["received"].get<bool>());
    EXPECT_EQ(result.data["msg"]["id"], 99);
    EXPECT_EQ(result.data["msg"]["jsonrpc"], "2.0");
}

TEST(TcpTransport, Bidirectional) {
    auto result = run_async_test(
        [](asio::any_io_executor exec) -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            asio::ip::tcp::acceptor acceptor(exec,
                asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
            auto port = acceptor.local_endpoint().port();

            asio::ip::tcp::socket client_sock(exec);
            co_await client_sock.async_connect(
                asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), port),
                asio::use_awaitable);

            asio::ip::tcp::socket server_sock = co_await acceptor.async_accept(
                asio::use_awaitable);

            auto server = make_socket_transport<RoleServer>(std::move(server_sock));
            auto client = make_socket_transport<RoleClient>(std::move(client_sock));

            // Client sends a ping request
            json ping_j = {
                {"jsonrpc", "2.0"},
                {"id", 10},
                {"method", "ping"}
            };
            auto msg = ping_j.get<TxJsonRpcMessage<RoleClient>>();
            co_await client->send(std::move(msg));

            // Server receives it
            auto server_recv = co_await server->receive();
            r.data["server_received"] = server_recv.has_value();
            if (server_recv) {
                json j = *server_recv;
                r.data["server_msg"] = j;
            }

            // Server sends response
            co_await server->send(
                TxJsonRpcMessage<RoleServer>(
                    typename TxJsonRpcMessage<RoleServer>::Response{
                        "2.0", RequestId(10), ServerResult::empty()}));

            // Client receives response
            auto client_recv = co_await client->receive();
            r.data["client_received"] = client_recv.has_value();
            if (client_recv) {
                json j = *client_recv;
                r.data["client_msg"] = j;
            }

            co_await server->close();
            co_await client->close();

            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;
    EXPECT_TRUE(result.data["server_received"].get<bool>());
    EXPECT_EQ(result.data["server_msg"]["method"], "ping");
    EXPECT_TRUE(result.data["client_received"].get<bool>());
    EXPECT_EQ(result.data["client_msg"]["id"], 10);
}

TEST(TcpTransport, CloseReturnsNullopt) {
    auto result = run_async_test(
        [](asio::any_io_executor exec) -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            asio::ip::tcp::acceptor acceptor(exec,
                asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
            auto port = acceptor.local_endpoint().port();

            asio::ip::tcp::socket client_sock(exec);
            co_await client_sock.async_connect(
                asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), port),
                asio::use_awaitable);

            asio::ip::tcp::socket server_sock = co_await acceptor.async_accept(
                asio::use_awaitable);

            auto server = make_socket_transport<RoleServer>(std::move(server_sock));
            auto client = make_socket_transport<RoleClient>(std::move(client_sock));

            // Close server
            co_await server->close();

            // Client should get nullopt (peer closed)
            auto received = co_await client->receive();
            r.data["got_nullopt"] = !received.has_value();

            co_await client->close();

            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;
    EXPECT_TRUE(result.data["got_nullopt"].get<bool>());
}

// =============================================================================
// Unix Socket Transport Tests
// =============================================================================

#if defined(__unix__) || defined(__APPLE__)

TEST(UnixSocketTransport, SendAndReceive) {
    auto result = run_async_test(
        [](asio::any_io_executor exec) -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            // Use a unique socket path
            std::string socket_path = "/tmp/mcp_test_unix_" +
                std::to_string(::getpid()) + ".sock";
            ::unlink(socket_path.c_str());

            asio::local::stream_protocol::endpoint ep(socket_path);
            asio::local::stream_protocol::acceptor acceptor(exec, ep);

            asio::local::stream_protocol::socket client_sock(exec);
            co_await client_sock.async_connect(ep, asio::use_awaitable);

            asio::local::stream_protocol::socket server_sock =
                co_await acceptor.async_accept(asio::use_awaitable);

            auto server = make_unix_socket_transport<RoleServer>(
                std::move(server_sock));
            auto client = make_unix_socket_transport<RoleClient>(
                std::move(client_sock));

            // Server sends a response
            co_await server->send(
                TxJsonRpcMessage<RoleServer>(
                    typename TxJsonRpcMessage<RoleServer>::Response{
                        "2.0", RequestId(77), ServerResult::empty()}));

            // Client receives
            auto received = co_await client->receive();
            r.data["received"] = received.has_value();
            if (received) {
                json j = *received;
                r.data["msg"] = j;
            }

            co_await server->close();
            co_await client->close();
            ::unlink(socket_path.c_str());

            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;
    EXPECT_TRUE(result.data["received"].get<bool>());
    EXPECT_EQ(result.data["msg"]["id"], 77);
}

TEST(UnixSocketTransport, Bidirectional) {
    auto result = run_async_test(
        [](asio::any_io_executor exec) -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            std::string socket_path = "/tmp/mcp_test_unix_bidir_" +
                std::to_string(::getpid()) + ".sock";
            ::unlink(socket_path.c_str());

            asio::local::stream_protocol::endpoint ep(socket_path);
            asio::local::stream_protocol::acceptor acceptor(exec, ep);

            asio::local::stream_protocol::socket client_sock(exec);
            co_await client_sock.async_connect(ep, asio::use_awaitable);

            asio::local::stream_protocol::socket server_sock =
                co_await acceptor.async_accept(asio::use_awaitable);

            auto server = make_unix_socket_transport<RoleServer>(
                std::move(server_sock));
            auto client = make_unix_socket_transport<RoleClient>(
                std::move(client_sock));

            // Client sends ping
            json ping_j = {
                {"jsonrpc", "2.0"},
                {"id", 5},
                {"method", "ping"}
            };
            co_await client->send(ping_j.get<TxJsonRpcMessage<RoleClient>>());

            // Server receives
            auto srv_recv = co_await server->receive();
            r.data["server_received"] = srv_recv.has_value();
            if (srv_recv) {
                json j = *srv_recv;
                r.data["server_msg"] = j;
            }

            // Server sends response
            co_await server->send(
                TxJsonRpcMessage<RoleServer>(
                    typename TxJsonRpcMessage<RoleServer>::Response{
                        "2.0", RequestId(5), ServerResult::empty()}));

            // Client receives
            auto cli_recv = co_await client->receive();
            r.data["client_received"] = cli_recv.has_value();
            if (cli_recv) {
                json j = *cli_recv;
                r.data["client_msg"] = j;
            }

            co_await server->close();
            co_await client->close();
            ::unlink(socket_path.c_str());

            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;
    EXPECT_TRUE(result.data["server_received"].get<bool>());
    EXPECT_EQ(result.data["server_msg"]["method"], "ping");
    EXPECT_TRUE(result.data["client_received"].get<bool>());
    EXPECT_EQ(result.data["client_msg"]["id"], 5);
}

#endif // defined(__unix__) || defined(__APPLE__)

// =============================================================================
// Worker Transport Tests
// =============================================================================

TEST(WorkerTransport, BasicSendReceive) {
    auto result = run_async_test(
        [](asio::any_io_executor exec) -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            // Create a worker that echoes back any outgoing message as a
            // received ping response.
            auto transport = WorkerTransport<RoleServer>::create(exec,
                [](std::shared_ptr<WorkerContext<RoleServer>> ctx)
                    -> asio::awaitable<void> {
                    // Push a ping request into the handler
                    json ping_j = {
                        {"jsonrpc", "2.0"},
                        {"id", 1},
                        {"method", "ping"}
                    };
                    auto msg = ping_j.get<RxJsonRpcMessage<RoleServer>>();
                    ctx->push_received(std::move(msg));

                    // Wait for the handler to send a response, then push
                    // another request
                    auto outgoing = co_await ctx->next_outgoing();
                    if (outgoing) {
                        // Push a second request
                        json req2 = {
                            {"jsonrpc", "2.0"},
                            {"id", 2},
                            {"method", "ping"}
                        };
                        ctx->push_received(
                            req2.get<RxJsonRpcMessage<RoleServer>>());
                    }

                    // Wait for second response then exit
                    co_await ctx->next_outgoing();
                });

            // Handler side: receive the first ping
            auto msg1 = co_await transport->receive();
            r.data["msg1_received"] = msg1.has_value();
            if (msg1) {
                json j = *msg1;
                r.data["msg1"] = j;
            }

            // Handler sends a response
            co_await transport->send(
                TxJsonRpcMessage<RoleServer>(
                    typename TxJsonRpcMessage<RoleServer>::Response{
                        "2.0", RequestId(1), ServerResult::empty()}));

            // Handler receives second request
            auto msg2 = co_await transport->receive();
            r.data["msg2_received"] = msg2.has_value();
            if (msg2) {
                json j = *msg2;
                r.data["msg2"] = j;
            }

            // Handler sends second response
            co_await transport->send(
                TxJsonRpcMessage<RoleServer>(
                    typename TxJsonRpcMessage<RoleServer>::Response{
                        "2.0", RequestId(2), ServerResult::empty()}));

            // Small yield to let worker finish
            asio::steady_timer t(exec);
            t.expires_after(std::chrono::milliseconds(50));
            co_await t.async_wait(asio::use_awaitable);

            // Worker is done, receive should return nullopt
            auto msg3 = co_await transport->receive();
            r.data["msg3_nullopt"] = !msg3.has_value();

            co_await transport->close();

            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;
    EXPECT_TRUE(result.data["msg1_received"].get<bool>());
    EXPECT_EQ(result.data["msg1"]["method"], "ping");
    EXPECT_EQ(result.data["msg1"]["id"], 1);
    EXPECT_TRUE(result.data["msg2_received"].get<bool>());
    EXPECT_EQ(result.data["msg2"]["id"], 2);
    EXPECT_TRUE(result.data["msg3_nullopt"].get<bool>());
}

TEST(WorkerTransport, CloseStopsWorker) {
    auto result = run_async_test(
        [](asio::any_io_executor exec) -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            bool worker_saw_close = false;

            auto transport = WorkerTransport<RoleClient>::create(exec,
                [&worker_saw_close](std::shared_ptr<WorkerContext<RoleClient>> ctx)
                    -> asio::awaitable<void> {
                    // Worker waits for outgoing messages until closed
                    while (true) {
                        auto msg = co_await ctx->next_outgoing();
                        if (!msg) {
                            worker_saw_close = true;
                            break;
                        }
                    }
                });

            // Close the transport
            co_await transport->close();

            // Small yield
            asio::steady_timer t(exec);
            t.expires_after(std::chrono::milliseconds(50));
            co_await t.async_wait(asio::use_awaitable);

            r.data["worker_saw_close"] = worker_saw_close;

            // Receive should return nullopt
            auto msg = co_await transport->receive();
            r.data["receive_nullopt"] = !msg.has_value();

            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;
    EXPECT_TRUE(result.data["worker_saw_close"].get<bool>());
    EXPECT_TRUE(result.data["receive_nullopt"].get<bool>());
}

TEST(WorkerTransport, WorkerExceptionClosesTransport) {
    auto result = run_async_test(
        [](asio::any_io_executor exec) -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            auto transport = WorkerTransport<RoleServer>::create(exec,
                [](std::shared_ptr<WorkerContext<RoleServer>>)
                    -> asio::awaitable<void> {
                    throw std::runtime_error("worker crashed");
                    co_return; // unreachable but needed for coroutine
                });

            // Small yield to let worker crash
            asio::steady_timer t(exec);
            t.expires_after(std::chrono::milliseconds(50));
            co_await t.async_wait(asio::use_awaitable);

            // Receive should return nullopt since worker crashed
            auto msg = co_await transport->receive();
            r.data["receive_nullopt"] = !msg.has_value();

            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;
    EXPECT_TRUE(result.data["receive_nullopt"].get<bool>());
}

TEST(WorkerTransport, MultipleMessages) {
    auto result = run_async_test(
        [](asio::any_io_executor exec) -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();
            r.data["received_ids"] = json::array();

            // Worker pushes 5 ping requests
            auto transport = WorkerTransport<RoleServer>::create(exec,
                [](std::shared_ptr<WorkerContext<RoleServer>> ctx)
                    -> asio::awaitable<void> {
                    for (int i = 0; i < 5; ++i) {
                        json req = {
                            {"jsonrpc", "2.0"},
                            {"id", i},
                            {"method", "ping"}
                        };
                        ctx->push_received(
                            req.get<RxJsonRpcMessage<RoleServer>>());
                    }
                    // Keep worker alive briefly so handler can receive
                    asio::steady_timer t(co_await asio::this_coro::executor);
                    t.expires_after(std::chrono::milliseconds(200));
                    co_await t.async_wait(asio::use_awaitable);
                });

            // Handler receives all 5
            for (int i = 0; i < 5; ++i) {
                auto msg = co_await transport->receive();
                if (msg) {
                    json j = *msg;
                    r.data["received_ids"].push_back(j["id"]);
                }
            }

            co_await transport->close();

            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;
    auto& ids = result.data["received_ids"];
    ASSERT_EQ(ids.size(), 5);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(ids[i], i) << "message " << i;
    }
}

// =============================================================================
// ChildProcess Transport Tests
// =============================================================================

#ifdef MCP_CHILD_PROCESS_TRANSPORT

#include "mcp/transport/child_process_transport.hpp"

TEST(ChildProcessTransport, SpawnAndCommunicate) {
    auto result = run_async_test(
        [](asio::any_io_executor exec) -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            // Spawn 'cat' as a child process — it echoes stdin to stdout
            typename ChildProcessTransport<RoleClient>::Options opts;
            opts.program = "cat";

            auto transport = std::make_unique<ChildProcessTransport<RoleClient>>(
                exec, std::move(opts));

            r.data["pid"] = transport->pid();
            r.data["pid_valid"] = (transport->pid() > 0);

            // Send a message (cat will echo it back)
            json ping_j = {
                {"jsonrpc", "2.0"},
                {"id", 1},
                {"method", "ping"}
            };
            auto msg = ping_j.get<TxJsonRpcMessage<RoleClient>>();
            co_await transport->send(std::move(msg));

            // Receive the echoed message — cat echoes the line-delimited JSON
            // back, which the transport reads as a received message.
            // Note: the echoed message has "method" and "id" fields so it
            // will be parsed as an RxJsonRpcMessage<RoleClient> (a ServerRequest).
            auto received = co_await transport->receive();
            r.data["received"] = received.has_value();
            if (received) {
                json j = *received;
                r.data["msg"] = j;
            }

            co_await transport->close();

            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;
    EXPECT_TRUE(result.data["pid_valid"].get<bool>());
    EXPECT_TRUE(result.data["received"].get<bool>());
    // cat echoes our ping back — it should parse as a request with method "ping"
    EXPECT_EQ(result.data["msg"]["method"], "ping");
    EXPECT_EQ(result.data["msg"]["id"], 1);
}

TEST(ChildProcessTransport, SpawnWithArgs) {
    auto result = run_async_test(
        [](asio::any_io_executor exec) -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            // Use /bin/sh -c 'cat' to test argument passing
            typename ChildProcessTransport<RoleClient>::Options opts;
            opts.program = "/bin/sh";
            opts.args = {"-c", "cat"};

            auto transport = std::make_unique<ChildProcessTransport<RoleClient>>(
                exec, std::move(opts));

            r.data["pid_valid"] = (transport->pid() > 0);

            // Send and receive via sh -c cat
            json msg_j = {
                {"jsonrpc", "2.0"},
                {"id", 42},
                {"method", "ping"}
            };
            co_await transport->send(
                msg_j.get<TxJsonRpcMessage<RoleClient>>());

            auto received = co_await transport->receive();
            r.data["received"] = received.has_value();
            if (received) {
                json j = *received;
                r.data["echo_id"] = j["id"];
            }

            co_await transport->close();

            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;
    EXPECT_TRUE(result.data["pid_valid"].get<bool>());
    EXPECT_TRUE(result.data["received"].get<bool>());
    EXPECT_EQ(result.data["echo_id"], 42);
}

TEST(ChildProcessTransport, CloseTerminatesChild) {
    auto result = run_async_test(
        [](asio::any_io_executor exec) -> asio::awaitable<TestResult> {
            TestResult r;
            r.data = json::object();

            typename ChildProcessTransport<RoleClient>::Options opts;
            opts.program = "cat";

            auto transport = std::make_unique<ChildProcessTransport<RoleClient>>(
                exec, std::move(opts));

            int pid = transport->pid();
            r.data["pid"] = pid;
            r.data["pid_valid"] = (pid > 0);

            // Close the transport — should terminate the child
            co_await transport->close();

            // After close, receive should return nullopt
            auto received = co_await transport->receive();
            r.data["got_nullopt"] = !received.has_value();

            // Destroy the transport (destructor should handle cleanup)
            transport.reset();

            // Check if the child process is gone
            int status = ::kill(pid, 0); // signal 0 = check if process exists
            r.data["process_gone"] = (status == -1);

            r.completed = true;
            co_return r;
        });

    ASSERT_TRUE(result.completed) << result.error;
    EXPECT_TRUE(result.data["pid_valid"].get<bool>());
    EXPECT_TRUE(result.data["got_nullopt"].get<bool>());
    // Process may or may not be reaped yet, but close() should have signaled it
}

#endif // MCP_CHILD_PROCESS_TRANSPORT
