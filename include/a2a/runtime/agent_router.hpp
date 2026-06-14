#pragma once

#include "agent_descriptor.hpp"
#include <optional>
#include <string>
#include <vector>

namespace a2a {

enum class RouteStrategy {
    CapabilityFirst,
    RoundRobin,
    LeastLoad
};

struct RouteQuery {
    std::string skill;
    std::string tag;
    std::string input_mode;
    std::string output_mode;
    std::string capability;
};

class AgentRouter {
public:
    explicit AgentRouter(RouteStrategy strategy = RouteStrategy::CapabilityFirst);

    std::optional<AgentDescriptor> select(const std::vector<AgentDescriptor>& agents,
                                          const RouteQuery& query);

    void set_strategy(RouteStrategy strategy) { strategy_ = strategy; }
    RouteStrategy strategy() const { return strategy_; }

private:
    bool matches_query(const AgentDescriptor& agent, const RouteQuery& query) const;
    std::optional<AgentDescriptor> select_round_robin(const std::vector<AgentDescriptor>& agents);
    std::optional<AgentDescriptor> select_least_load(const std::vector<AgentDescriptor>& agents) const;

    RouteStrategy strategy_;
    size_t cursor_ = 0;
};

} // namespace a2a
