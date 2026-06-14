#pragma once

#include "../runtime/task_event.hpp"
#include <optional>
#include <string>
#include <vector>

namespace a2a {

class IEventStore {
public:
    virtual ~IEventStore() = default;

    virtual void append_event(const TaskEvent& event) = 0;
    virtual std::vector<TaskEvent> list_events(const std::string& task_id) = 0;
    virtual std::vector<TaskEvent> list_events_by_trace(const std::string& trace_id) = 0;
    virtual std::optional<TaskEvent> get_latest_event(const std::string& task_id) = 0;
};

} // namespace a2a
