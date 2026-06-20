#include <a2a/routing/semantic_agent_router.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <cassert>
#include <atomic>
#include <cmath>
#include <iostream>
#include <utility>
#include <vector>

using namespace a2a;

namespace {

AgentDescriptor agent(const std::string& id, const std::string& skill_name,
                      const std::string& description, double load = 0.0,
                      std::string health = "healthy", std::vector<std::string> tags = {"local"}) {
    AgentDescriptor value;
    value.set_agent_id(id);
    value.set_name(id);
    value.set_address("http://127.0.0.1:1");
    value.set_health(std::move(health));
    value.set_load(load);
    value.set_tags(std::move(tags));
    AgentSkillDescriptor skill;
    skill.name = skill_name;
    skill.description = description;
    value.set_skills({skill});
    return value;
}

class CountingEmbeddingProvider final : public routing::IEmbeddingProvider {
public:
    std::string model() const override { return "counting-test"; }

    std::vector<float> embed(const std::string& text, routing::EmbeddingInputType type) override {
        if (type == routing::EmbeddingInputType::Query) ++query_calls;
        else ++document_calls;
        return text.find("coding") != std::string::npos
            ? std::vector<float>{1.0f, 0.0f}
            : std::vector<float>{0.0f, 1.0f};
    }

    std::atomic<int> query_calls{0};
    std::atomic<int> document_calls{0};
};

} // namespace

int main() {
    routing::FakeEmbeddingProvider provider;
    const auto first = provider.embed("coding c++ implementation", routing::EmbeddingInputType::Query);
    const auto second = provider.embed("coding c++ implementation", routing::EmbeddingInputType::Query);
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

    boost::asio::io_context cache_io;
    auto counting = std::make_unique<CountingEmbeddingProvider>();
    auto* counters = counting.get();
    routing::SemanticAgentRouter cache_router(std::move(counting));
    std::vector<AgentDescriptor> cache_agents{
        agent("architect", "architecture", "system design"),
        agent("coder", "coding", "coding implementation"),
        agent("offline", "coding", "coding implementation", 0.0, "offline"),
        agent("remote", "coding", "coding implementation", 0.0, "healthy", {"remote"})
    };
    auto first_route = boost::asio::co_spawn(cache_io,
        cache_router.select(cache_agents, "coding task", "local", 0.0), boost::asio::use_future);
    cache_io.run();
    assert(first_route.get()->agent.agent_id() == "coder");
    assert(counters->document_calls == 2);
    assert(counters->query_calls == 1);

    cache_io.restart();
    auto cached_route = boost::asio::co_spawn(cache_io,
        cache_router.select(cache_agents, "coding task", "local", 0.0), boost::asio::use_future);
    cache_io.run();
    assert(cached_route.get()->agent.agent_id() == "coder");
    assert(counters->document_calls == 2);
    assert(counters->query_calls == 1);

    cache_agents[1] = agent("coder", "coding", "coding implementation changed");
    cache_io.restart();
    auto changed_route = boost::asio::co_spawn(cache_io,
        cache_router.select(cache_agents, "coding task", "local", 0.0), boost::asio::use_future);
    cache_io.run();
    assert(changed_route.get()->agent.agent_id() == "coder");
    assert(counters->document_calls == 3);
    assert(counters->query_calls == 1);

    std::cout << "semantic_router_test passed\n";
    return 0;
}
