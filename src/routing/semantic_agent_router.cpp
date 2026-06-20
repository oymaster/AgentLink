#include <a2a/routing/semantic_agent_router.hpp>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <curl/curl.h>
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace a2a::routing {
namespace asio = boost::asio;
using json = nlohmann::json;

namespace {

size_t write_body(char* data, size_t size, size_t count, void* target) {
    static_cast<std::string*>(target)->append(data, size * count);
    return size * count;
}

void normalize(std::vector<float>& values) {
    double norm = 0.0;
    for (const auto value : values) norm += value * value;
    if (norm == 0.0) return;
    const auto scale = static_cast<float>(1.0 / std::sqrt(norm));
    for (auto& value : values) value *= scale;
}

std::string hash_string(const std::string& value) {
    std::ostringstream out;
    out << std::hex << std::hash<std::string>{}(value);
    return out.str();
}

} // namespace

std::vector<float> FakeEmbeddingProvider::embed(const std::string& text, EmbeddingInputType) {
    std::vector<float> result(dimensions_, 0.0f);
    std::string token;
    auto add_token = [&] {
        if (token.empty()) return;
        result[std::hash<std::string>{}(token) % dimensions_] += 1.0f;
        token.clear();
    };
    for (const unsigned char ch : text) {
        if (std::isalnum(ch) || ch >= 0x80) token.push_back(static_cast<char>(std::tolower(ch)));
        else add_token();
    }
    add_token();
    normalize(result);
    return result;
}

DashScopeEmbeddingProvider::DashScopeEmbeddingProvider(std::string api_key, long timeout_ms)
    : api_key_(std::move(api_key)), timeout_ms_(timeout_ms) {
    if (api_key_.empty()) throw std::invalid_argument("DASHSCOPE_API_KEY is required");
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

std::vector<float> DashScopeEmbeddingProvider::embed(const std::string& text, EmbeddingInputType type) {
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), &curl_easy_cleanup);
    if (!curl) throw std::runtime_error("curl_easy_init failed");
    const auto text_type = type == EmbeddingInputType::Query ? "query" : "document";
    const auto request = json{{"model", model()}, {"input", {{"texts", json::array({text})}}},
                              {"parameters", {{"text_type", text_type}}}}.dump();
    std::string response;
    curl_slist* raw_headers = nullptr;
    raw_headers = curl_slist_append(raw_headers, "Content-Type: application/json");
    const auto authorization = "Authorization: Bearer " + api_key_;
    raw_headers = curl_slist_append(raw_headers, authorization.c_str());
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(raw_headers, &curl_slist_free_all);
    curl_easy_setopt(curl.get(), CURLOPT_URL,
        "https://dashscope.aliyuncs.com/api/v1/services/embeddings/text-embedding/text-embedding");
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, request.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(request.size()));
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, timeout_ms_);
    const auto code = curl_easy_perform(curl.get());
    if (code != CURLE_OK) throw std::runtime_error(curl_easy_strerror(code));
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status < 200 || status >= 300) throw std::runtime_error("DashScope HTTP " + std::to_string(status));
    const auto document = json::parse(response);
    if (!document.contains("output") || !document["output"].contains("embeddings") ||
        document["output"]["embeddings"].empty()) {
        throw std::runtime_error(document.value("message", "DashScope response has no embedding"));
    }
    return document["output"]["embeddings"][0]["embedding"].get<std::vector<float>>();
}

SemanticAgentRouter::SemanticAgentRouter(std::unique_ptr<IEmbeddingProvider> provider,
                                         std::size_t cache_capacity,
                                         std::chrono::seconds cache_ttl)
    : provider_(std::move(provider)), cache_capacity_(std::max<std::size_t>(1, cache_capacity)),
      cache_ttl_(cache_ttl) {
    if (!provider_) throw std::invalid_argument("embedding provider is required");
}

SemanticAgentRouter::~SemanticAgentRouter() { pool_.join(); }

std::vector<float> SemanticAgentRouter::embed_cached(const std::string& text, EmbeddingInputType type) {
    const auto type_name = type == EmbeddingInputType::Query ? "query" : "document";
    const auto key = provider_->model() + ":" + type_name + ":" + hash_string(text);
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(mutex_);
        const auto it = cache_.find(key);
        if (it != cache_.end() && now - it->second.created < cache_ttl_) {
            lru_.splice(lru_.begin(), lru_, it->second.lru);
            return it->second.value;
        }
        if (it != cache_.end()) {
            lru_.erase(it->second.lru);
            cache_.erase(it);
        }
    }
    auto value = provider_->embed(text, type);
    {
        std::lock_guard lock(mutex_);
        while (cache_.size() >= cache_capacity_) {
            cache_.erase(lru_.back());
            lru_.pop_back();
        }
        lru_.push_front(key);
        cache_[key] = CacheEntry{value, now, lru_.begin()};
    }
    return value;
}

std::string SemanticAgentRouter::document_for(const AgentDescriptor& agent, const AgentSkillDescriptor& skill) {
    std::ostringstream text;
    text << agent.name() << ' ' << skill.name << ' ' << skill.description;
    for (const auto& tag : agent.tags()) text << ' ' << tag;
    return text.str();
}

double SemanticAgentRouter::cosine(const std::vector<float>& lhs, const std::vector<float>& rhs) {
    if (lhs.empty() || lhs.size() != rhs.size()) return 0.0;
    double dot = 0.0, left = 0.0, right = 0.0;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        dot += lhs[i] * rhs[i]; left += lhs[i] * lhs[i]; right += rhs[i] * rhs[i];
    }
    return left == 0.0 || right == 0.0 ? 0.0 : dot / (std::sqrt(left) * std::sqrt(right));
}

asio::awaitable<std::optional<SemanticRouteResult>> SemanticAgentRouter::select(
    std::vector<AgentDescriptor> agents, std::string message, std::string tag, double threshold) {
    auto ranked = co_await rank(std::move(agents), std::move(message), std::move(tag), 1, threshold);
    if (ranked.empty()) co_return std::nullopt;
    co_return ranked.front();
}

asio::awaitable<std::vector<SemanticRouteResult>> SemanticAgentRouter::rank(
    std::vector<AgentDescriptor> agents, std::string message, std::string tag,
    std::size_t top_k, double threshold) {
    co_return co_await asio::co_spawn(pool_,
        [this, agents = std::move(agents), message = std::move(message), tag = std::move(tag), threshold, top_k]() mutable
            -> asio::awaitable<std::vector<SemanticRouteResult>> {
            std::unordered_set<std::string> live;
            for (const auto& agent : agents) {
                if (!agent.is_healthy() || !agent.has_tag(tag)) continue;
                for (const auto& skill : agent.skills()) {
                    const auto key = agent.agent_id() + ":" + skill.name;
                    const auto document = document_for(agent, skill);
                    const auto content_hash = hash_string(document);
                    live.insert(key);
                    auto found = index_.find(key);
                    if (found == index_.end() || found->second.content_hash != content_hash) {
                        index_[key] = IndexEntry{
                            agent, content_hash, embed_cached(document, EmbeddingInputType::Document)};
                    } else {
                        found->second.agent = agent;
                    }
                }
            }
            for (auto it = index_.begin(); it != index_.end();) {
                if (!live.contains(it->first)) it = index_.erase(it); else ++it;
            }
            const auto query = embed_cached(message, EmbeddingInputType::Query);
            std::vector<SemanticRouteResult> ranked;
            for (const auto& [_, entry] : index_) {
                const auto semantic = cosine(query, entry.embedding);
                if (semantic < threshold) continue;
                const auto load = std::clamp(entry.agent.load(), 0.0, 1.0);
                const auto final_score = 0.85 * semantic + 0.15 * (1.0 - load);
                ranked.push_back(SemanticRouteResult{entry.agent, semantic, final_score});
            }
            std::sort(ranked.begin(), ranked.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.final_score > rhs.final_score;
            });
            if (ranked.size() > top_k) ranked.resize(top_k);
            co_return ranked;
        }, asio::use_awaitable);
}

} // namespace a2a::routing
