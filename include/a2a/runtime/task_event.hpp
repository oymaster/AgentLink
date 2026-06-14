#pragma once

#include <map>
#include <string>

namespace a2a {

enum class TaskEventType {
    Created,
    Dispatched,
    Running,
    Message,
    Artifact,
    Completed,
    Failed
};

std::string to_string(TaskEventType type);
TaskEventType task_event_type_from_string(const std::string& type);

class TaskEvent {
public:
    TaskEvent() = default;

    const std::string& event_id() const { return event_id_; }
    const std::string& trace_id() const { return trace_id_; }
    const std::string& task_id() const { return task_id_; }
    const std::string& context_id() const { return context_id_; }
    const std::string& source_agent_id() const { return source_agent_id_; }
    const std::string& target_agent_id() const { return target_agent_id_; }
    TaskEventType type() const { return type_; }
    long long sequence() const { return sequence_; }
    long long timestamp_ms() const { return timestamp_ms_; }
    const std::string& payload_json() const { return payload_json_; }
    const std::map<std::string, std::string>& metadata() const { return metadata_; }

    void set_event_id(const std::string& value) { event_id_ = value; }
    void set_trace_id(const std::string& value) { trace_id_ = value; }
    void set_task_id(const std::string& value) { task_id_ = value; }
    void set_context_id(const std::string& value) { context_id_ = value; }
    void set_source_agent_id(const std::string& value) { source_agent_id_ = value; }
    void set_target_agent_id(const std::string& value) { target_agent_id_ = value; }
    void set_type(TaskEventType value) { type_ = value; }
    void set_sequence(long long value) { sequence_ = value; }
    void set_timestamp_ms(long long value) { timestamp_ms_ = value; }
    void set_payload_json(const std::string& value) { payload_json_ = value; }
    void set_metadata(const std::map<std::string, std::string>& value) { metadata_ = value; }
    void set_metadata_value(const std::string& key, const std::string& value) { metadata_[key] = value; }

    std::string to_json() const;
    static TaskEvent from_json(const std::string& json_str);

    static TaskEvent create(TaskEventType type,
                            const std::string& trace_id,
                            const std::string& task_id,
                            const std::string& source_agent_id = "",
                            const std::string& target_agent_id = "");

private:
    std::string event_id_;
    std::string trace_id_;
    std::string task_id_;
    std::string context_id_;
    std::string source_agent_id_;
    std::string target_agent_id_;
    TaskEventType type_ = TaskEventType::Created;
    long long sequence_ = 0;
    long long timestamp_ms_ = 0;
    std::string payload_json_ = "{}";
    std::map<std::string, std::string> metadata_;
};

} // namespace a2a
