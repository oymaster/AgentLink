#pragma once

#include "../models/agent_message.hpp"
#include <map>
#include <string>

namespace a2a {

class TaskEnvelope {
public:
    TaskEnvelope() = default;

    const std::string& trace_id() const { return trace_id_; }
    const std::string& task_id() const { return task_id_; }
    const std::string& context_id() const { return context_id_; }
    const std::string& parent_task_id() const { return parent_task_id_; }
    const std::string& idempotency_key() const { return idempotency_key_; }
    const std::string& source_agent_id() const { return source_agent_id_; }
    const std::string& target_agent_id() const { return target_agent_id_; }
    const std::string& method() const { return method_; }
    const std::string& intent() const { return intent_; }
    const AgentMessage& message() const { return message_; }
    const std::map<std::string, std::string>& metadata() const { return metadata_; }

    void set_trace_id(const std::string& value) { trace_id_ = value; }
    void set_task_id(const std::string& value) { task_id_ = value; }
    void set_context_id(const std::string& value) { context_id_ = value; }
    void set_parent_task_id(const std::string& value) { parent_task_id_ = value; }
    void set_idempotency_key(const std::string& value) { idempotency_key_ = value; }
    void set_source_agent_id(const std::string& value) { source_agent_id_ = value; }
    void set_target_agent_id(const std::string& value) { target_agent_id_ = value; }
    void set_method(const std::string& value) { method_ = value; }
    void set_intent(const std::string& value) { intent_ = value; }
    void set_message(const AgentMessage& value) { message_ = value; }
    void set_metadata(const std::map<std::string, std::string>& value) { metadata_ = value; }
    void set_metadata_value(const std::string& key, const std::string& value) { metadata_[key] = value; }

    std::string to_json() const;
    static TaskEnvelope from_json(const std::string& json_str);

    static TaskEnvelope create(const std::string& source_agent_id,
                               const std::string& target_agent_id,
                               const AgentMessage& message,
                               const std::string& method = "message/send",
                               const std::string& intent = "");

private:
    std::string trace_id_;
    std::string task_id_;
    std::string context_id_;
    std::string parent_task_id_;
    std::string idempotency_key_;
    std::string source_agent_id_;
    std::string target_agent_id_;
    std::string method_ = "message/send";
    std::string intent_;
    AgentMessage message_;
    std::map<std::string, std::string> metadata_;
};

} // namespace a2a
