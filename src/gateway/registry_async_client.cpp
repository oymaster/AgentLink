#include <a2a/gateway/registry_async_client.hpp>

#include <json.hpp>
#include <stdexcept>

namespace a2a::gateway {
using json = nlohmann::json;

RegistryAsyncClient::RegistryAsyncClient(std::string base_url, std::chrono::milliseconds timeout)
    : base_url_(std::move(base_url)), timeout_(timeout) {
    while (!base_url_.empty() && base_url_.back() == '/') base_url_.pop_back();
}

boost::asio::awaitable<std::vector<AgentDescriptor>> RegistryAsyncClient::list_agents() const {
    const auto response = co_await http_.request("GET", base_url_ + "/v1/agents", "", timeout_);
    if (response.status < 200 || response.status >= 300) {
        throw std::runtime_error("registry HTTP " + std::to_string(response.status));
    }
    const auto document = json::parse(response.body);
    if (document.value("success", true) == false) {
        throw std::runtime_error(document.value("error", "registry returned failure"));
    }
    std::vector<AgentDescriptor> agents;
    if (document.contains("agents") && document["agents"].is_array()) {
        for (const auto& item : document["agents"]) agents.push_back(AgentDescriptor::from_json(item.dump()));
    }
    co_return agents;
}

} // namespace a2a::gateway
