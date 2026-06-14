#include <a2a/runtime/agent_router.hpp>
#include <algorithm>

namespace a2a {

AgentRouter::AgentRouter(RouteStrategy strategy)
    : strategy_(strategy) {}

std::optional<AgentDescriptor> AgentRouter::select(const std::vector<AgentDescriptor>& agents,
                                                   const RouteQuery& query) {
    std::vector<AgentDescriptor> candidates;
    for (const auto& agent : agents) {
        if (matches_query(agent, query)) {
            candidates.push_back(agent);
        }
    }

    if (candidates.empty()) {
        return std::nullopt;
    }

    if (strategy_ == RouteStrategy::RoundRobin) {
        return select_round_robin(candidates);
    }

    if (strategy_ == RouteStrategy::LeastLoad) {
        return select_least_load(candidates);
    }

    return select_least_load(candidates);
}

bool AgentRouter::matches_query(const AgentDescriptor& agent, const RouteQuery& query) const {
    return agent.is_healthy()
        && agent.has_tag(query.tag)
        && agent.has_skill(query.skill)
        && agent.supports_input_mode(query.input_mode)
        && agent.supports_output_mode(query.output_mode)
        && agent.capability_enabled(query.capability);
}

std::optional<AgentDescriptor> AgentRouter::select_round_robin(const std::vector<AgentDescriptor>& agents) {
    if (agents.empty()) {
        return std::nullopt;
    }

    const auto index = cursor_ % agents.size();
    cursor_ = (cursor_ + 1) % agents.size();
    return agents[index];
}

std::optional<AgentDescriptor> AgentRouter::select_least_load(const std::vector<AgentDescriptor>& agents) const {
    if (agents.empty()) {
        return std::nullopt;
    }

    return *std::min_element(agents.begin(), agents.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.load() < rhs.load();
    });
}

} // namespace a2a
