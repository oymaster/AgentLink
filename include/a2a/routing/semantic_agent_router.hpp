#pragma once

#include <a2a/runtime/agent_descriptor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/thread_pool.hpp>
#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace a2a::routing {

class IEmbeddingProvider {
public:
    virtual ~IEmbeddingProvider() = default;
    virtual std::string model() const = 0;
    virtual std::vector<float> embed(const std::string& text) = 0;
};

class FakeEmbeddingProvider final : public IEmbeddingProvider {
public:
    explicit FakeEmbeddingProvider(std::size_t dimensions = 256) : dimensions_(dimensions) {}
    std::string model() const override { return "fake-token-hash-v1"; }
    std::vector<float> embed(const std::string& text) override;
private:
    std::size_t dimensions_;
};

class DashScopeEmbeddingProvider final : public IEmbeddingProvider {
public:
    DashScopeEmbeddingProvider(std::string api_key, long timeout_ms);
    std::string model() const override { return "text-embedding-v2"; }
    std::vector<float> embed(const std::string& text) override;
private:
    std::string api_key_;
    long timeout_ms_;
};

struct SemanticRouteResult {
    AgentDescriptor agent;
    double semantic_score = 0.0;
    double final_score = 0.0;
};

class SemanticAgentRouter {
public:
    explicit SemanticAgentRouter(std::unique_ptr<IEmbeddingProvider> provider,
                                 std::size_t cache_capacity = 1024,
                                 std::chrono::seconds cache_ttl = std::chrono::hours(24));
    ~SemanticAgentRouter();

    boost::asio::awaitable<std::optional<SemanticRouteResult>> select(
        std::vector<AgentDescriptor> agents,
        std::string message,
        std::string tag,
        double threshold = 0.30);
    boost::asio::awaitable<std::vector<SemanticRouteResult>> rank(
        std::vector<AgentDescriptor> agents,
        std::string message,
        std::string tag,
        std::size_t top_k,
        double threshold = 0.30);

private:
    struct CacheEntry {
        std::vector<float> value;
        std::chrono::steady_clock::time_point created;
        std::list<std::string>::iterator lru;
    };
    struct IndexEntry {
        AgentDescriptor agent;
        std::string content_hash;
        std::vector<float> embedding;
    };

    std::vector<float> embed_cached(const std::string& text);
    static std::string document_for(const AgentDescriptor& agent, const AgentSkillDescriptor& skill);
    static double cosine(const std::vector<float>& lhs, const std::vector<float>& rhs);

    std::unique_ptr<IEmbeddingProvider> provider_;
    boost::asio::thread_pool pool_{1};
    std::size_t cache_capacity_;
    std::chrono::seconds cache_ttl_;
    std::mutex mutex_;
    std::list<std::string> lru_;
    std::unordered_map<std::string, CacheEntry> cache_;
    std::unordered_map<std::string, IndexEntry> index_;
};

} // namespace a2a::routing
