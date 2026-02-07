#pragma once

#include <any>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <boost/asio.hpp>

#include "mcp/model/task.hpp"
#include "mcp/model/tool.hpp"
#include "mcp/service/cancellation_token.hpp"

namespace mcp {

namespace asio = boost::asio;

/// Describes a submitted operation for task tracking.
struct OperationDescriptor {
    std::string task_id;
    TaskStatus status = TaskStatus::Running;
    CancellationToken cancellation;
    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point updated_at;
    std::optional<std::chrono::seconds> timeout;
    std::any result;
};

/// Configuration for OperationProcessor.
struct OperationProcessorConfig {
    /// Default timeout for operations
    std::chrono::seconds default_timeout = std::chrono::seconds{300};
    /// TTL for completed results before cleanup
    std::chrono::seconds result_ttl = std::chrono::seconds{600};
    /// Interval for cleanup sweeps
    std::chrono::seconds cleanup_interval = std::chrono::seconds{60};
};

/// Manages long-running tool invocations with timeout, polling,
/// TTL-based cleanup, and result collection.
///
/// Port of Rust's OperationProcessor.
class OperationProcessor {
public:
    using Config = OperationProcessorConfig;

    explicit OperationProcessor(asio::any_io_executor executor, Config config = {});
    ~OperationProcessor();

    /// Submit an async operation for task tracking.
    /// Returns a task ID.
    template <typename F>
    std::string submit(F&& operation, std::optional<std::chrono::seconds> timeout = std::nullopt);

    /// Get the status of a task.
    std::optional<Task> get_task_info(const std::string& task_id) const;

    /// List all known tasks.
    std::vector<Task> list_tasks() const;

    /// Get the result of a completed task.
    std::optional<CallToolResult> get_task_result(const std::string& task_id) const;

    /// Cancel a running task.
    bool cancel_task(const std::string& task_id);

    /// Get the number of active operations.
    size_t active_count() const;

private:
    void start_cleanup_timer();
    void cleanup_expired();
    std::string generate_task_id();

    asio::any_io_executor executor_;
    Config config_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<OperationDescriptor>> operations_;
    std::unique_ptr<asio::steady_timer> cleanup_timer_;
    uint64_t id_counter_ = 0;
};

} // namespace mcp
