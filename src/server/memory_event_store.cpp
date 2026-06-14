#include <a2a/server/memory_event_store.hpp>

namespace a2a {

void MemoryEventStore::append_event(const TaskEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto stored = event;
    if (stored.sequence() <= 0) {
        stored.set_sequence(static_cast<long long>(events_by_task_[event.task_id()].size() + 1));
    }
    events_by_task_[stored.task_id()].push_back(stored);
    events_by_trace_[stored.trace_id()].push_back(stored);
}

std::vector<TaskEvent> MemoryEventStore::list_events(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = events_by_task_.find(task_id);
    if (it == events_by_task_.end()) {
        return {};
    }
    return it->second;
}

std::vector<TaskEvent> MemoryEventStore::list_events_by_trace(const std::string& trace_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = events_by_trace_.find(trace_id);
    if (it == events_by_trace_.end()) {
        return {};
    }
    return it->second;
}

std::optional<TaskEvent> MemoryEventStore::get_latest_event(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = events_by_task_.find(task_id);
    if (it == events_by_task_.end() || it->second.empty()) {
        return std::nullopt;
    }
    return it->second.back();
}

size_t MemoryEventStore::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& pair : events_by_task_) {
        count += pair.second.size();
    }
    return count;
}

void MemoryEventStore::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    events_by_task_.clear();
    events_by_trace_.clear();
}

} // namespace a2a
