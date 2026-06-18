#include <a2a/gateway/gateway_config.hpp>

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <thread>

namespace a2a::gateway {
namespace {

std::string env_string(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

long env_long(const char* name, long fallback) {
    const auto value = env_string(name, "");
    if (value.empty()) return fallback;
    try {
        return std::stol(value);
    } catch (...) {
        throw std::runtime_error(std::string("invalid integer environment variable: ") + name);
    }
}

} // namespace

GatewayConfig GatewayConfig::from_environment() {
    GatewayConfig config;
    config.port = static_cast<unsigned short>(env_long("AGENTLINK_GATEWAY_PORT", 5002));
    config.registry_url = env_string("AGENTLINK_REGISTRY_URL", config.registry_url);
    config.redis_host = env_string("AGENTLINK_REDIS_HOST", config.redis_host);
    config.redis_port = static_cast<int>(env_long("AGENTLINK_REDIS_PORT", config.redis_port));
    config.agent_timeout_ms = env_long("AGENTLINK_AGENT_TIMEOUT_MS", config.agent_timeout_ms);
    config.registry_timeout_ms = env_long("AGENTLINK_REGISTRY_TIMEOUT_MS", config.registry_timeout_ms);
    config.embedding_timeout_ms = env_long("AGENTLINK_EMBEDDING_TIMEOUT_MS", config.embedding_timeout_ms);
    const auto hardware = std::max(1u, std::thread::hardware_concurrency());
    config.io_threads = static_cast<std::size_t>(env_long("AGENTLINK_IO_THREADS", std::min(4u, hardware)));
    config.embedding_provider = env_string("AGENTLINK_EMBEDDING_PROVIDER", "fake");
    config.dashscope_api_key = env_string("DASHSCOPE_API_KEY", "");
    if (config.io_threads == 0) config.io_threads = 1;
    return config;
}

} // namespace a2a::gateway
