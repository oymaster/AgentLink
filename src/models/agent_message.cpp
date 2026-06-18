#include <a2a/models/agent_message.hpp>
#include <json.hpp>
#include <sstream>

namespace a2a {

std::string AgentMessage::to_json() const {
    std::ostringstream oss;
    oss << "{";
    
    // Required fields
    oss << "\"messageId\":\"" << message_id_ << "\",";
    oss << "\"role\":\"" << to_string(role_) << "\"";
    
    // Optional fields
    if (context_id_.has_value()) {
        oss << ",\"contextId\":\"" << *context_id_ << "\"";
    }
    
    if (task_id_.has_value()) {
        oss << ",\"taskId\":\"" << *task_id_ << "\"";
    }
    
    // Parts array
    oss << ",\"parts\":[";
    for (size_t i = 0; i < parts_.size(); ++i) {
        if (i > 0) oss << ",";
        oss << parts_[i]->to_json();
    }
    oss << "]";
    
    oss << "}";
    return oss.str();
}

AgentMessage AgentMessage::from_json(const std::string& json) {
    AgentMessage msg;
    const auto parsed = nlohmann::json::parse(json);
    msg.message_id_ = parsed.value("messageId", "");
    msg.role_ = message_role_from_string(parsed.value("role", "user"));
    if (parsed.contains("contextId") && parsed["contextId"].is_string())
        msg.context_id_ = parsed["contextId"].get<std::string>();
    if (parsed.contains("taskId") && parsed["taskId"].is_string())
        msg.task_id_ = parsed["taskId"].get<std::string>();
    if (parsed.contains("parts") && parsed["parts"].is_array()) {
        for (const auto& value : parsed["parts"]) {
            auto part = Part::from_json(value.dump());
            if (part) msg.parts_.push_back(std::move(part));
        }
    }
    return msg;
}

} // namespace a2a
