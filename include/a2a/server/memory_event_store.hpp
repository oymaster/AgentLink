#pragma once

#include "event_store.hpp"
#include <map>
#include <mutex>

namespace a2a {

class MemoryEventStore : public IEventStore {
public:
    void append_event(const TaskEvent& event) override;
    std::vector<TaskEvent> list_events(const std::string& task_id) override;
    std::vector<TaskEvent> list_events_by_trace(const std::string& trace_id) override;
    std::optional<TaskEvent> get_latest_event(const std::string& task_id) override;

    size_t size() const;
    void clear();

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::vector<TaskEvent>> events_by_task_;
    std::map<std::string, std::vector<TaskEvent>> events_by_trace_;
};

} // namespace a2a
