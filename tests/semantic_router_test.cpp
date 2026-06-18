#include <a2a/routing/semantic_agent_router.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <cassert>
#include <cmath>
#include <iostream>

using namespace a2a;

namespace {

AgentDescriptor agent(const std::string& id, const std::string& skill_name,
                      const std::string& description, double load = 0.0) {
    AgentDescriptor value;
    value.set_agent_id(id);
    value.set_name(id);
    value.set_address("http://127.0.0.1:1");
    value.set_health("healthy");
    value.set_load(load);
    value.set_tags({"local"});
    AgentSkillDescriptor skill;
    skill.name = skill_name;
    skill.description = description;
    value.set_skills({skill});
    return value;
}

} // namespace

int main() {
    routing::FakeEmbeddingProvider provider;
    const auto first = provider.embed("coding c++ implementation");
    const auto second = provider.embed("coding c++ implementation");
    assert(first == second);

    boost::asio::io_context io;
    routing::SemanticAgentRouter router(std::make_unique<routing::FakeEmbeddingProvider>());
    std::vector<AgentDescriptor> agents{
        agent("architect", "architecture", "system architecture requirements interfaces"),
        agent("coder", "coding", "coding c++ implementation tests")
    };
    auto selected = boost::asio::co_spawn(io,
        router.select(agents, "coding c++ implementation", "local", 0.30),
        boost::asio::use_future);
    io.run();
    const auto result = selected.get();
    assert(result.has_value());
    assert(result->agent.agent_id() == "coder");
    assert(result->semantic_score >= 0.30);

    std::cout << "semantic_router_test passed\n";
    return 0;
}
