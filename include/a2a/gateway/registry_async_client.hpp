#pragma once

#include <a2a/gateway/beast_http_client.hpp>
#include <a2a/runtime/agent_descriptor.hpp>
#include <boost/asio/awaitable.hpp>
#include <chrono>
#include <string>
#include <vector>

namespace a2a::gateway {

class RegistryAsyncClient {
public:
    RegistryAsyncClient(std::string base_url, std::chrono::milliseconds timeout);
    boost::asio::awaitable<std::vector<AgentDescriptor>> list_agents() const;

private:
    std::string base_url_;
    std::chrono::milliseconds timeout_;
    BeastHttpClient http_;
};

} // namespace a2a::gateway
