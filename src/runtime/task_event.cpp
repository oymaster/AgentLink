#include <a2a/runtime/task_event.hpp>
#include <chrono>
#include <json.hpp>
#include <random>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

namespace a2a {
namespace {

long long now_ms() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

std::string make_id(const std::string& prefix) {
    static std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<unsigned long long> dist;
    std::ostringstream oss;
    oss << prefix << "-" << now_ms() << "-" << std::hex << dist(rng);
    return oss.str();
}

json payload_to_json(const std::string& payload_json) {
    if (payload_json.empty()) {
        return json::object();
    }

    try {
        return json::parse(payload_json);
    } catch (const json::parse_error&) {
        return payload_json;
    }
}

std::string payload_from_json(const json& value) {
    if (value.is_null()) {
        return "{}";
    }
    if (value.is_string()) {
        return value.get<std::string>();
    }
    return value.dump();
}

} // namespace

std::string to_string(TaskEventType type) {
    switch (type) {
        case TaskEventType::Created:
            return "task.created";
        case TaskEventType::Dispatched:
            return "task.dispatched";
        case TaskEventType::Running:
            return "task.running";
        case TaskEventType::Message:
            return "task.message";
        case TaskEventType::Artifact:
            return "task.artifact";
        case TaskEventType::Completed:
            return "task.completed";
        case TaskEventType::Failed:
            return "task.failed";
    }
    return "task.created";
}

TaskEventType task_event_type_from_string(const std::string& type) {
    if (type == "task.created") return TaskEventType::Created;
    if (type == "task.dispatched") return TaskEventType::Dispatched;
    if (type == "task.running") return TaskEventType::Running;
    if (type == "task.message") return TaskEventType::Message;
    if (type == "task.artifact") return TaskEventType::Artifact;
    if (type == "task.completed") return TaskEventType::Completed;
    if (type == "task.failed") return TaskEventType::Failed;
    throw std::invalid_argument("Unknown task event type: " + type);
}

std::string TaskEvent::to_json() const {
    json j;
    j["event_id"] = event_id_;
    j["trace_id"] = trace_id_;
    j["task_id"] = task_id_;
    j["context_id"] = context_id_;
    j["source_agent_id"] = source_agent_id_;
    j["target_agent_id"] = target_agent_id_;
    j["type"] = to_string(type_);
    j["sequence"] = sequence_;
    j["timestamp_ms"] = timestamp_ms_;
    j["payload"] = payload_to_json(payload_json_);
    j["metadata"] = metadata_;
    return j.dump();
}

TaskEvent TaskEvent::from_json(const std::string& json_str) {
    const auto j = json::parse(json_str);
    TaskEvent event;
    event.event_id_ = j.value("event_id", "");
    event.trace_id_ = j.value("trace_id", "");
    event.task_id_ = j.value("task_id", "");
    event.context_id_ = j.value("context_id", "");
    event.source_agent_id_ = j.value("source_agent_id", "");
    event.target_agent_id_ = j.value("target_agent_id", "");
    event.type_ = task_event_type_from_string(j.value("type", "task.created"));
    event.sequence_ = j.value("sequence", 0LL);
    event.timestamp_ms_ = j.value("timestamp_ms", 0LL);
    event.payload_json_ = j.contains("payload") ? payload_from_json(j["payload"]) : "{}";
    event.metadata_ = j.value("metadata", std::map<std::string, std::string>{});
    return event;
}

TaskEvent TaskEvent::create(TaskEventType type,
                            const std::string& trace_id,
                            const std::string& task_id,
                            const std::string& source_agent_id,
                            const std::string& target_agent_id) {
    TaskEvent event;
    event.event_id_ = make_id("evt");
    event.trace_id_ = trace_id;
    event.task_id_ = task_id;
    event.source_agent_id_ = source_agent_id;
    event.target_agent_id_ = target_agent_id;
    event.type_ = type;
    event.timestamp_ms_ = now_ms();
    return event;
}

} // namespace a2a
