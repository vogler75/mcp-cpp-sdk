#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mcp/model/types.hpp"
#include "mcp/model/meta.hpp"
#include "mcp/model/content.hpp"

namespace mcp {

// =============================================================================
// TaskStatus
// =============================================================================

enum class TaskStatus {
    Running,
    Completed,
    Failed,
    Canceled,
};

inline void to_json(json& j, const TaskStatus& s) {
    switch (s) {
        case TaskStatus::Running: j = "running"; break;
        case TaskStatus::Completed: j = "completed"; break;
        case TaskStatus::Failed: j = "failed"; break;
        case TaskStatus::Canceled: j = "canceled"; break;
    }
}

inline void from_json(const json& j, TaskStatus& s) {
    auto v = j.get<std::string>();
    if (v == "running") {
        s = TaskStatus::Running;
    } else if (v == "completed") {
        s = TaskStatus::Completed;
    } else if (v == "failed") {
        s = TaskStatus::Failed;
    } else if (v == "canceled") {
        s = TaskStatus::Canceled;
    } else {
        throw json::other_error::create(501, "Unknown TaskStatus: " + v, &j);
    }
}

// =============================================================================
// Task
// =============================================================================

struct Task {
    std::string id;
    TaskStatus status;
    std::optional<Meta> meta;

    bool operator==(const Task& other) const {
        return id == other.id && status == other.status && meta == other.meta;
    }

    friend void to_json(json& j, const Task& t) {
        j = json{{"id", t.id}, {"status", t.status}};
        if (t.meta.has_value()) {
            j["_meta"] = *t.meta;
        }
    }

    friend void from_json(const json& j, Task& t) {
        j.at("id").get_to(t.id);
        j.at("status").get_to(t.status);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            t.meta = j["_meta"].get<Meta>();
        }
    }
};

// =============================================================================
// CreateTaskResult
// =============================================================================

struct CreateTaskResult {
    Task task;

    bool operator==(const CreateTaskResult& other) const { return task == other.task; }

    friend void to_json(json& j, const CreateTaskResult& r) {
        j = json{{"task", r.task}};
    }

    friend void from_json(const json& j, CreateTaskResult& r) {
        j.at("task").get_to(r.task);
    }
};

// =============================================================================
// TaskResult
// =============================================================================

struct TaskResult {
    std::vector<Content> content;
    std::optional<json> structured_content;
    std::optional<bool> is_error;
    std::optional<Meta> meta;

    bool operator==(const TaskResult& other) const {
        return content == other.content && structured_content == other.structured_content
            && is_error == other.is_error && meta == other.meta;
    }

    friend void to_json(json& j, const TaskResult& r) {
        j = json{{"content", r.content}};
        detail::set_opt(j, "structuredContent", r.structured_content);
        detail::set_opt(j, "isError", r.is_error);
        if (r.meta.has_value()) {
            j["_meta"] = *r.meta;
        }
    }

    friend void from_json(const json& j, TaskResult& r) {
        j.at("content").get_to(r.content);
        detail::get_opt(j, "structuredContent", r.structured_content);
        detail::get_opt(j, "isError", r.is_error);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            r.meta = j["_meta"].get<Meta>();
        }
    }
};

// =============================================================================
// GetTaskInfoParams
// =============================================================================

struct GetTaskInfoParams {
    std::optional<Meta> meta;
    std::string task_id;

    bool operator==(const GetTaskInfoParams& other) const {
        return meta == other.meta && task_id == other.task_id;
    }

    friend void to_json(json& j, const GetTaskInfoParams& p) {
        j = json{{"taskId", p.task_id}};
        if (p.meta.has_value()) {
            j["_meta"] = *p.meta;
        }
    }

    friend void from_json(const json& j, GetTaskInfoParams& p) {
        j.at("taskId").get_to(p.task_id);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            p.meta = j["_meta"].get<Meta>();
        }
    }
};

// =============================================================================
// GetTaskInfoResult
// =============================================================================

struct GetTaskInfoResult {
    std::optional<Task> task;

    bool operator==(const GetTaskInfoResult& other) const { return task == other.task; }

    friend void to_json(json& j, const GetTaskInfoResult& r) {
        j = json::object();
        if (r.task.has_value()) {
            j["task"] = *r.task;
        }
    }

    friend void from_json(const json& j, GetTaskInfoResult& r) {
        if (j.contains("task") && !j["task"].is_null()) {
            r.task = j["task"].get<Task>();
        }
    }
};

// =============================================================================
// GetTaskResultParams
// =============================================================================

struct GetTaskResultParams {
    std::optional<Meta> meta;
    std::string task_id;

    bool operator==(const GetTaskResultParams& other) const {
        return meta == other.meta && task_id == other.task_id;
    }

    friend void to_json(json& j, const GetTaskResultParams& p) {
        j = json{{"taskId", p.task_id}};
        if (p.meta.has_value()) {
            j["_meta"] = *p.meta;
        }
    }

    friend void from_json(const json& j, GetTaskResultParams& p) {
        j.at("taskId").get_to(p.task_id);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            p.meta = j["_meta"].get<Meta>();
        }
    }
};

// =============================================================================
// CancelTaskParams
// =============================================================================

struct CancelTaskParams {
    std::optional<Meta> meta;
    std::string task_id;

    bool operator==(const CancelTaskParams& other) const {
        return meta == other.meta && task_id == other.task_id;
    }

    friend void to_json(json& j, const CancelTaskParams& p) {
        j = json{{"taskId", p.task_id}};
        if (p.meta.has_value()) {
            j["_meta"] = *p.meta;
        }
    }

    friend void from_json(const json& j, CancelTaskParams& p) {
        j.at("taskId").get_to(p.task_id);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            p.meta = j["_meta"].get<Meta>();
        }
    }
};

// =============================================================================
// ListTasksResult
// =============================================================================

struct ListTasksResult {
    std::vector<Task> tasks;
    std::optional<std::string> next_cursor;
    std::optional<uint64_t> total;

    bool operator==(const ListTasksResult& other) const {
        return tasks == other.tasks && next_cursor == other.next_cursor && total == other.total;
    }

    friend void to_json(json& j, const ListTasksResult& r) {
        j = json{{"tasks", r.tasks}};
        detail::set_opt(j, "nextCursor", r.next_cursor);
        detail::set_opt(j, "total", r.total);
    }

    friend void from_json(const json& j, ListTasksResult& r) {
        j.at("tasks").get_to(r.tasks);
        detail::get_opt(j, "nextCursor", r.next_cursor);
        detail::get_opt(j, "total", r.total);
    }
};

} // namespace mcp
