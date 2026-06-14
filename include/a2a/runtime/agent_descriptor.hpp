#pragma once

#include <map>
#include <string>
#include <vector>

namespace a2a {

struct AgentSkillDescriptor {
    std::string name;
    std::string description;
    std::vector<std::string> input_modes;
    std::vector<std::string> output_modes;
};

class AgentDescriptor {
public:
    const std::string& agent_id() const { return agent_id_; }
    const std::string& name() const { return name_; }
    const std::string& address() const { return address_; }
    const std::string& version() const { return version_; }
    const std::string& platform() const { return platform_; }
    const std::string& runtime() const { return runtime_; }
    const std::vector<std::string>& tags() const { return tags_; }
    const std::vector<AgentSkillDescriptor>& skills() const { return skills_; }
    const std::vector<std::string>& input_modes() const { return input_modes_; }
    const std::vector<std::string>& output_modes() const { return output_modes_; }
    const std::map<std::string, bool>& capabilities() const { return capabilities_; }
    bool auth_required() const { return auth_required_; }
    const std::string& health() const { return health_; }
    double load() const { return load_; }
    long long last_heartbeat_ms() const { return last_heartbeat_ms_; }
    const std::map<std::string, std::string>& metadata() const { return metadata_; }

    void set_agent_id(const std::string& value) { agent_id_ = value; }
    void set_name(const std::string& value) { name_ = value; }
    void set_address(const std::string& value) { address_ = value; }
    void set_version(const std::string& value) { version_ = value; }
    void set_platform(const std::string& value) { platform_ = value; }
    void set_runtime(const std::string& value) { runtime_ = value; }
    void set_tags(const std::vector<std::string>& value) { tags_ = value; }
    void set_skills(const std::vector<AgentSkillDescriptor>& value) { skills_ = value; }
    void set_input_modes(const std::vector<std::string>& value) { input_modes_ = value; }
    void set_output_modes(const std::vector<std::string>& value) { output_modes_ = value; }
    void set_capabilities(const std::map<std::string, bool>& value) { capabilities_ = value; }
    void set_auth_required(bool value) { auth_required_ = value; }
    void set_health(const std::string& value) { health_ = value; }
    void set_load(double value) { load_ = value; }
    void set_last_heartbeat_ms(long long value) { last_heartbeat_ms_ = value; }
    void set_metadata(const std::map<std::string, std::string>& value) { metadata_ = value; }

    bool has_tag(const std::string& tag) const;
    bool has_skill(const std::string& skill) const;
    bool supports_input_mode(const std::string& mode) const;
    bool supports_output_mode(const std::string& mode) const;
    bool capability_enabled(const std::string& capability) const;
    bool is_healthy() const;

    std::string to_json() const;
    static AgentDescriptor from_json(const std::string& json_str);

private:
    std::string agent_id_;
    std::string name_;
    std::string address_;
    std::string version_;
    std::string platform_;
    std::string runtime_;
    std::vector<std::string> tags_;
    std::vector<AgentSkillDescriptor> skills_;
    std::vector<std::string> input_modes_;
    std::vector<std::string> output_modes_;
    std::map<std::string, bool> capabilities_;
    bool auth_required_ = false;
    std::string health_ = "unknown";
    double load_ = 0.0;
    long long last_heartbeat_ms_ = 0;
    std::map<std::string, std::string> metadata_;
};

} // namespace a2a
