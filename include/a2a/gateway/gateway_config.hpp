#pragma once

#include <cstddef>
#include <string>

namespace a2a::gateway {

struct GatewayConfig {
    unsigned short port = 5002;
    std::string registry_url = "http://127.0.0.1:8500";
    std::string redis_host = "127.0.0.1";
    int redis_port = 6379;
    long agent_timeout_ms = 600000;
    long registry_timeout_ms = 5000;
    long embedding_timeout_ms = 10000;
    std::size_t io_threads = 1;
    std::string embedding_provider = "fake";
    std::string dashscope_api_key;

    static GatewayConfig from_environment();
};

} // namespace a2a::gateway
