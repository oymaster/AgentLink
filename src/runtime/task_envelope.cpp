#include <a2a/runtime/task_envelope.hpp>
#include <chrono>
#include <json.hpp>
#include <random>
#include <sstream>

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

} // namespace

std::string TaskEnvelope::to_json() const {
    json j;
    j["trace_id"] = trace_id_;
    j["task_id"] = task_id_;
    j["context_id"] = context_id_;
    j["parent_task_id"] = parent_task_id_;
    j["idempotency_key"] = idempotency_key_;
    j["source_agent_id"] = source_agent_id_;
    j["target_agent_id"] = target_agent_id_;
    j["method"] = method_;
    j["intent"] = intent_;
    j["message"] = json::parse(message_.to_json());
    j["metadata"] = metadata_;
    return j.dump();
}

TaskEnvelope TaskEnvelope::from_json(const std::string& json_str) {
    const auto j = json::parse(json_str);
    TaskEnvelope envelope;
    envelope.trace_id_ = j.value("trace_id", "");
    envelope.task_id_ = j.value("task_id", "");
    envelope.context_id_ = j.value("context_id", "");
    envelope.parent_task_id_ = j.value("parent_task_id", "");
    envelope.idempotency_key_ = j.value("idempotency_key", "");
    envelope.source_agent_id_ = j.value("source_agent_id", "");
    envelope.target_agent_id_ = j.value("target_agent_id", "");
    envelope.method_ = j.value("method", "message/send");
    envelope.intent_ = j.value("intent", "");
    if (j.contains("message")) {
        envelope.message_ = AgentMessage::from_json(j["message"].dump());
    }
    envelope.metadata_ = j.value("metadata", std::map<std::string, std::string>{});
    return envelope;
}

TaskEnvelope TaskEnvelope::create(const std::string& source_agent_id,
                                  const std::string& target_agent_id,
                                  const AgentMessage& message,
                                  const std::string& method,
                                  const std::string& intent) {
    TaskEnvelope envelope;
    envelope.trace_id_ = make_id("trace");
    envelope.task_id_ = make_id("task");
    envelope.context_id_ = make_id("ctx");
    envelope.idempotency_key_ = make_id("idem");
    envelope.source_agent_id_ = source_agent_id;
    envelope.target_agent_id_ = target_agent_id;
    envelope.method_ = method;
    envelope.intent_ = intent;
    envelope.message_ = message;
    envelope.message_.set_task_id(envelope.task_id_);
    envelope.message_.set_context_id(envelope.context_id_);
    return envelope;
}

} // namespace a2a
