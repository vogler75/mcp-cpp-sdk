#include "mcp/task_manager/operation_processor.hpp"
#include <spdlog/spdlog.h>

namespace mcp {

OperationProcessor::OperationProcessor(asio::any_io_executor executor, Config config)
    : executor_(std::move(executor))
    , config_(config)
    , cleanup_timer_(std::make_unique<asio::steady_timer>(executor_)) {
    start_cleanup_timer();
}

OperationProcessor::~OperationProcessor() {
    cleanup_timer_->cancel();
}

std::optional<Task> OperationProcessor::get_task_info(const std::string& task_id) const {
    std::lock_guard lock(mutex_);
    auto it = operations_.find(task_id);
    if (it == operations_.end()) return std::nullopt;
    Task task;
    task.id = it->second->task_id;
    task.status = it->second->status;
    return task;
}

std::vector<Task> OperationProcessor::list_tasks() const {
    std::lock_guard lock(mutex_);
    std::vector<Task> tasks;
    tasks.reserve(operations_.size());
    for (const auto& [id, desc] : operations_) {
        Task task;
        task.id = desc->task_id;
        task.status = desc->status;
        tasks.push_back(std::move(task));
    }
    return tasks;
}

std::optional<CallToolResult> OperationProcessor::get_task_result(const std::string& task_id) const {
    std::lock_guard lock(mutex_);
    auto it = operations_.find(task_id);
    if (it == operations_.end()) return std::nullopt;
    if (it->second->status != TaskStatus::Completed) return std::nullopt;
    try {
        return std::any_cast<CallToolResult>(it->second->result);
    } catch (const std::bad_any_cast&) {
        return std::nullopt;
    }
}

bool OperationProcessor::cancel_task(const std::string& task_id) {
    std::lock_guard lock(mutex_);
    auto it = operations_.find(task_id);
    if (it == operations_.end()) return false;
    if (it->second->status != TaskStatus::Running) return false;
    it->second->cancellation.cancel();
    it->second->status = TaskStatus::Canceled;
    it->second->updated_at = std::chrono::steady_clock::now();
    return true;
}

size_t OperationProcessor::active_count() const {
    std::lock_guard lock(mutex_);
    size_t count = 0;
    for (const auto& [_, desc] : operations_) {
        if (desc->status == TaskStatus::Running) ++count;
    }
    return count;
}

void OperationProcessor::start_cleanup_timer() {
    cleanup_timer_->expires_after(config_.cleanup_interval);
    cleanup_timer_->async_wait([this](boost::system::error_code ec) {
        if (!ec) {
            cleanup_expired();
            start_cleanup_timer();
        }
    });
}

void OperationProcessor::cleanup_expired() {
    std::lock_guard lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    for (auto it = operations_.begin(); it != operations_.end();) {
        auto& desc = it->second;
        if (desc->status != TaskStatus::Running) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - desc->updated_at);
            if (age > config_.result_ttl) {
                it = operations_.erase(it);
                continue;
            }
        }
        if (desc->status == TaskStatus::Running && desc->timeout) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - desc->created_at);
            if (elapsed > *desc->timeout) {
                desc->cancellation.cancel();
                desc->status = TaskStatus::Canceled;
                desc->updated_at = now;
            }
        }
        ++it;
    }
}

std::string OperationProcessor::generate_task_id() {
    return "task-" + std::to_string(++id_counter_);
}

} // namespace mcp
