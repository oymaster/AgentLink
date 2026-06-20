#include <a2a/routing/semantic_agent_router.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <json.hpp>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace a2a;
using json = nlohmann::json;

namespace {

struct Query {
    std::optional<std::string> expected;
    std::string text;
};

struct Dataset {
    std::vector<AgentDescriptor> agents;
    std::vector<Query> queries;
};

struct Observation {
    bool positive = false;
    bool top1_correct = false;
    int expected_rank = 0;
    std::string query;
    std::string expected;
    std::string selected;
    double top_semantic_score = 0.0;
    double latency_ms = 0.0;
};

Dataset load_dataset(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open semantic evaluation dataset: " + path);
    json document;
    input >> document;
    Dataset result;
    for (const auto& item : document.at("agents")) {
        AgentDescriptor agent;
        agent.set_agent_id(item.at("id").get<std::string>());
        agent.set_name(item.at("name").get<std::string>());
        agent.set_address("http://127.0.0.1:1");
        agent.set_health("healthy");
        agent.set_load(item.value("load", 0.1));
        auto tags = item.at("tags").get<std::vector<std::string>>();
        tags.push_back("local");
        agent.set_tags(std::move(tags));
        AgentSkillDescriptor skill;
        skill.name = item.at("skill").get<std::string>();
        skill.description = item.at("description").get<std::string>();
        agent.set_skills({std::move(skill)});
        result.agents.push_back(std::move(agent));
    }
    for (const auto& item : document.at("queries")) {
        Query query;
        if (!item.at("expected").is_null()) query.expected = item.at("expected").get<std::string>();
        query.text = item.at("query").get<std::string>();
        result.queries.push_back(std::move(query));
    }
    return result;
}

double ratio(std::size_t value, std::size_t total) {
    return total == 0 ? 0.0 : static_cast<double>(value) / static_cast<double>(total);
}

double percentile95(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(0.95 * static_cast<double>(values.size() - 1));
    return values[index];
}

void evaluate(const Dataset& dataset, std::unique_ptr<routing::IEmbeddingProvider> provider) {
    const auto provider_name = provider->model();
    boost::asio::io_context io;
    routing::SemanticAgentRouter router(std::move(provider));
    auto future = boost::asio::co_spawn(io, [&]() -> boost::asio::awaitable<std::vector<Observation>> {
        std::vector<Observation> observations;
        for (const auto& query : dataset.queries) {
            const auto started = std::chrono::steady_clock::now();
            const auto ranked = co_await router.rank(dataset.agents, query.text, "local", 3, 0.0);
            Observation observation;
            observation.positive = query.expected.has_value();
            observation.query = query.text;
            observation.expected = query.expected.value_or("");
            observation.selected = ranked.empty() ? "" : ranked.front().agent.agent_id();
            observation.top_semantic_score = ranked.empty() ? 0.0 : ranked.front().semantic_score;
            observation.latency_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            if (query.expected) {
                for (std::size_t rank = 0; rank < ranked.size(); ++rank) {
                    if (ranked[rank].agent.agent_id() != *query.expected) continue;
                    observation.expected_rank = static_cast<int>(rank + 1);
                    observation.top1_correct = rank == 0;
                    break;
                }
            }
            observations.push_back(observation);
        }
        co_return observations;
    }, boost::asio::use_future);
    io.run();
    const auto observations = future.get();

    std::size_t positives = 0, negatives = 0, recall1 = 0, recall3 = 0;
    std::size_t routed_correct_at_default = 0, false_routes_at_default = 0;
    std::size_t positive_abstentions_at_default = 0;
    double reciprocal_rank = 0.0;
    std::vector<double> latencies;
    for (const auto& item : observations) {
        latencies.push_back(item.latency_ms);
        if (item.positive) {
            ++positives;
            if (item.expected_rank == 1) ++recall1;
            if (item.expected_rank > 0 && item.expected_rank <= 3) ++recall3;
            if (item.expected_rank > 0) reciprocal_rank += 1.0 / static_cast<double>(item.expected_rank);
            if (item.top_semantic_score < routing::kDefaultSemanticThreshold) {
                ++positive_abstentions_at_default;
            }
            if (item.top1_correct && item.top_semantic_score >= routing::kDefaultSemanticThreshold) {
                ++routed_correct_at_default;
            }
        } else {
            ++negatives;
            if (item.top_semantic_score >= routing::kDefaultSemanticThreshold) ++false_routes_at_default;
        }
    }

    double recommended_threshold = 1.0;
    double recommended_recall = 0.0;
    for (int step = 0; step <= 100; ++step) {
        const double threshold = static_cast<double>(step) / 100.0;
        std::size_t correct = 0, false_routes = 0;
        for (const auto& item : observations) {
            if (item.positive && item.top1_correct && item.top_semantic_score >= threshold) ++correct;
            if (!item.positive && item.top_semantic_score >= threshold) ++false_routes;
        }
        const auto false_route_rate = ratio(false_routes, negatives);
        const auto routed_recall = ratio(correct, positives);
        if (false_route_rate <= 0.05 && routed_recall > recommended_recall) {
            recommended_threshold = threshold;
            recommended_recall = routed_recall;
        }
    }

    const auto cold_start_latency = latencies.empty() ? 0.0 : latencies.front();
    std::vector<double> warm_latencies;
    if (latencies.size() > 1) warm_latencies.assign(latencies.begin() + 1, latencies.end());
    const auto warm_average_latency = warm_latencies.empty() ? 0.0
        : std::accumulate(warm_latencies.begin(), warm_latencies.end(), 0.0) /
            static_cast<double>(warm_latencies.size());
    std::cout << std::fixed << std::setprecision(3)
              << "provider=" << provider_name
              << " positives=" << positives
              << " negatives=" << negatives
              << " recall@1=" << ratio(recall1, positives)
              << " recall@3=" << ratio(recall3, positives)
              << " mrr=" << (positives == 0 ? 0.0 : reciprocal_rank / static_cast<double>(positives))
              << " threshold=" << routing::kDefaultSemanticThreshold
              << " routed_recall=" << ratio(routed_correct_at_default, positives)
              << " false_route_rate=" << ratio(false_routes_at_default, negatives)
              << " positive_abstentions=" << positive_abstentions_at_default
              << " recommended_threshold=" << recommended_threshold
              << " recommended_recall=" << recommended_recall
              << " cold_start_latency_ms=" << cold_start_latency
              << " warm_avg_latency_ms=" << warm_average_latency
              << " warm_p95_latency_ms=" << percentile95(std::move(warm_latencies)) << '\n';

    if (provider_name != "fake-token-hash-v1") {
        for (const auto& item : observations) {
            if (item.positive && !item.top1_correct) {
                std::cout << json{{"provider", provider_name}, {"kind", "top1_miss"},
                    {"expected", item.expected}, {"selected", item.selected},
                    {"score", item.top_semantic_score}, {"query", item.query}}.dump() << '\n';
            }
            if (item.positive && item.top_semantic_score < routing::kDefaultSemanticThreshold) {
                std::cout << json{{"provider", provider_name}, {"kind", "abstained_at_default_threshold"},
                    {"threshold", routing::kDefaultSemanticThreshold},
                    {"expected", item.expected}, {"selected", item.selected},
                    {"score", item.top_semantic_score}, {"query", item.query}}.dump() << '\n';
            }
        }
    }
}

} // namespace

int main() {
    try {
        const char* override_path = std::getenv("AGENTLINK_SEMANTIC_EVAL_DATA");
        const std::string path = override_path && *override_path ? override_path : SEMANTIC_EVAL_DATA_PATH;
        const auto dataset = load_dataset(path);
        evaluate(dataset, std::make_unique<routing::FakeEmbeddingProvider>());
        const char* api_key = std::getenv("DASHSCOPE_API_KEY");
        if (api_key && *api_key) {
            evaluate(dataset, std::make_unique<routing::DashScopeEmbeddingProvider>(api_key, 10000));
        } else {
            std::cout << "provider=text-embedding-v2 skipped reason=DASHSCOPE_API_KEY_not_set\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "semantic_router_eval failed: " << error.what() << '\n';
        return 1;
    }
}
