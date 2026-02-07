#pragma once

#ifdef MCP_HTTP_TRANSPORT

#include <string>
#include <optional>
#include <memory>
#include <functional>
#include <queue>
#include <mutex>
#include <atomic>
#include <chrono>
#include <algorithm>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>

#include "mcp/transport/transport.hpp"
#include "mcp/service/service_role.hpp"

namespace mcp {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;
using json = nlohmann::json;

/// Configuration for the streamable HTTP client transport.
struct StreamableHttpClientConfig {
    /// The full URL to connect to (e.g., "http://localhost:8080/mcp")
    std::string url;

    /// Optional Bearer token for Authorization header
    std::optional<std::string> auth_header;

    /// SSE retry policy
    struct SseRetryPolicy {
        int max_retries = 3;
        std::chrono::milliseconds base_interval{1000};
        enum class Type {
            ExponentialBackoff,
            FixedInterval,
            Never,
        } type = Type::ExponentialBackoff;

        std::chrono::milliseconds delay_for(int attempt) const {
            switch (type) {
                case Type::ExponentialBackoff:
                    return base_interval * (1 << std::min(attempt, 10));
                case Type::FixedInterval:
                    return base_interval;
                case Type::Never:
                default:
                    return std::chrono::milliseconds{0};
            }
        }
    };
    SseRetryPolicy sse_retry;
};

/// Parsed SSE event
struct SseEvent {
    std::optional<std::string> id;
    std::optional<std::string> event;
    std::string data;
    std::optional<int> retry_ms;
};

/// Parse SSE events from a text/event-stream body.
/// Returns a vector of parsed events.
std::vector<SseEvent> parse_sse_events(const std::string& body);

/// Parse URL into components
struct ParsedUrl {
    std::string host;
    std::string port;
    std::string target; // path + query
    bool is_https = false;
};
ParsedUrl parse_url(const std::string& url);

/// Streamable HTTP client transport.
///
/// Implements Transport<R> by POSTing JSON-RPC messages to an HTTP
/// endpoint and receiving responses. The server may respond with:
/// - Direct JSON (single response)
/// - SSE stream (multiple messages, used for initialize and streaming)
///
/// The client also supports opening a GET-based SSE stream for receiving
/// server-initiated messages (notifications, requests).
template <typename R>
class StreamableHttpClientTransport : public Transport<R> {
public:
    explicit StreamableHttpClientTransport(
        asio::any_io_executor executor,
        StreamableHttpClientConfig config);

    ~StreamableHttpClientTransport() override;

    asio::awaitable<void> send(TxJsonRpcMessage<R> msg) override;
    asio::awaitable<std::optional<RxJsonRpcMessage<R>>> receive() override;
    asio::awaitable<void> close() override;

private:
    asio::any_io_executor executor_;
    StreamableHttpClientConfig config_;
    ParsedUrl parsed_url_;

    std::optional<std::string> session_id_;
    std::optional<std::string> last_event_id_;

    // Receive queue: messages received from SSE or POST responses
    std::queue<RxJsonRpcMessage<R>> receive_queue_;
    std::mutex queue_mutex_;
    std::shared_ptr<asio::steady_timer> queue_signal_;

    bool closed_ = false;

    /// Make an HTTP POST request with a JSON-RPC message body
    asio::awaitable<http::response<http::string_body>> do_post(
        const std::string& body);

    /// Send an HTTP DELETE request to terminate the session
    asio::awaitable<void> do_delete();

    /// Parse SSE response body and enqueue all contained messages
    void enqueue_sse_messages(const std::string& body);

    /// Enqueue a single received message and signal any waiting receiver
    void enqueue_message(RxJsonRpcMessage<R> msg);

    /// Build common request headers on an HTTP request
    template <typename Body>
    void set_common_headers(http::request<Body>& req);
};

} // namespace mcp

#endif // MCP_HTTP_TRANSPORT
