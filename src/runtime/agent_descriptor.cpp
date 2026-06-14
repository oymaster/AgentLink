#include <a2a/runtime/agent_descriptor.hpp>
#include <algorithm>
#include <json.hpp>

using json = nlohmann::json;

namespace a2a {
namespace {

bool contains_string(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

json skill_to_json(const AgentSkillDescriptor& skill) {
    return {
        {"name", skill.name},
        {"description", skill.description},
        {"input_modes", skill.input_modes},
        {"output_modes", skill.output_modes}
    };
}

AgentSkillDescriptor skill_from_json(const json& j) {
    AgentSkillDescriptor skill;
    skill.name = j.value("name", "");
    skill.description = j.value("description", "");
    skill.input_modes = j.value("input_modes", std::vector<std::string>{});
    skill.output_modes = j.value("output_modes", std::vector<std::string>{});
    return skill;
}

} // namespace

bool AgentDescriptor::has_tag(const std::string& tag) const {
    return tag.empty() || contains_string(tags_, tag);
}

bool AgentDescriptor::has_skill(const std::string& skill) const {
    if (skill.empty()) {
        return true;
    }
    return std::any_of(skills_.begin(), skills_.end(), [&](const AgentSkillDescriptor& item) {
        return item.name == skill;
    });
}

bool AgentDescriptor::supports_input_mode(const std::string& mode) const {
    if (mode.empty()) {
        return true;
    }
    if (contains_string(input_modes_, mode)) {
        return true;
    }
    return std::any_of(skills_.begin(), skills_.end(), [&](const AgentSkillDescriptor& skill) {
        return contains_string(skill.input_modes, mode);
    });
}

bool AgentDescriptor::supports_output_mode(const std::string& mode) const {
    if (mode.empty()) {
        return true;
    }
    if (contains_string(output_modes_, mode)) {
        return true;
    }
    return std::any_of(skills_.begin(), skills_.end(), [&](const AgentSkillDescriptor& skill) {
        return contains_string(skill.output_modes, mode);
    });
}

bool AgentDescriptor::capability_enabled(const std::string& capability) const {
    if (capability.empty()) {
        return true;
    }
    const auto it = capabilities_.find(capability);
    return it != capabilities_.end() && it->second;
}

bool AgentDescriptor::is_healthy() const {
    return health_.empty() || health_ == "healthy" || health_ == "unknown";
}

std::string AgentDescriptor::to_json() const {
    json skills = json::array();
    for (const auto& skill : skills_) {
        skills.push_back(skill_to_json(skill));
    }

    json j;
    j["agent_id"] = agent_id_;
    j["name"] = name_;
    j["address"] = address_;
    j["version"] = version_;
    j["platform"] = platform_;
    j["runtime"] = runtime_;
    j["tags"] = tags_;
    j["skills"] = skills;
    j["input_modes"] = input_modes_;
    j["output_modes"] = output_modes_;
    j["capabilities"] = capabilities_;
    j["auth_required"] = auth_required_;
    j["health"] = health_;
    j["load"] = load_;
    j["last_heartbeat_ms"] = last_heartbeat_ms_;
    j["metadata"] = metadata_;
    return j.dump();
}

AgentDescriptor AgentDescriptor::from_json(const std::string& json_str) {
    const auto j = json::parse(json_str);
    AgentDescriptor descriptor;
    descriptor.agent_id_ = j.value("agent_id", j.value("id", ""));
    descriptor.name_ = j.value("name", "");
    descriptor.address_ = j.value("address", "");
    descriptor.version_ = j.value("version", "");
    descriptor.platform_ = j.value("platform", "");
    descriptor.runtime_ = j.value("runtime", "");
    descriptor.tags_ = j.value("tags", std::vector<std::string>{});
    descriptor.input_modes_ = j.value("input_modes", std::vector<std::string>{});
    descriptor.output_modes_ = j.value("output_modes", std::vector<std::string>{});
    descriptor.capabilities_ = j.value("capabilities", std::map<std::string, bool>{});
    descriptor.auth_required_ = j.value("auth_required", false);
    descriptor.health_ = j.value("health", "unknown");
    descriptor.load_ = j.value("load", 0.0);
    descriptor.last_heartbeat_ms_ = j.value("last_heartbeat_ms", 0LL);
    descriptor.metadata_ = j.value("metadata", std::map<std::string, std::string>{});

    if (j.contains("skills") && j["skills"].is_array()) {
        for (const auto& item : j["skills"]) {
            descriptor.skills_.push_back(skill_from_json(item));
        }
    }

    return descriptor;
}

} // namespace a2a
