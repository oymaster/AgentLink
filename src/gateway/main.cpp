#include <a2a/gateway/event_store_executor.hpp>
#include <a2a/gateway/gateway_config.hpp>
#include <a2a/gateway/http_server.hpp>
#include <a2a/gateway/registry_async_client.hpp>
#include <a2a/gateway/task_engine.hpp>
#include <a2a/routing/semantic_agent_router.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <csignal>
#include <iostream>
#include <thread>
#include <vector>

namespace asio = boost::asio;

int main() {
    try {
        auto config = a2a::gateway::GatewayConfig::from_environment();
        asio::io_context io(static_cast<int>(config.io_threads));
        auto events = std::make_shared<a2a::gateway::EventStoreExecutor>(config.redis_host, config.redis_port);
        auto registry = std::make_shared<a2a::gateway::RegistryAsyncClient>(config.registry_url,
            std::chrono::milliseconds(config.registry_timeout_ms));
        std::unique_ptr<a2a::routing::IEmbeddingProvider> provider;
        if (config.embedding_provider == "dashscope") {
            provider = std::make_unique<a2a::routing::DashScopeEmbeddingProvider>(
                config.dashscope_api_key, config.embedding_timeout_ms);
        } else {
            provider = std::make_unique<a2a::routing::FakeEmbeddingProvider>();
        }
        auto semantic = std::make_shared<a2a::routing::SemanticAgentRouter>(std::move(provider));
        auto engine = std::make_shared<a2a::gateway::TaskEngine>(io.get_executor(), config, events, registry, semantic);
        auto server = std::make_shared<a2a::gateway::HttpServer>(io.get_executor(), config.port, engine);
        asio::co_spawn(io, server->run(), asio::detached);

        asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([server, engine, &io](const boost::system::error_code& error, int) {
            if (error) return;
            server->stop();
            asio::co_spawn(io, [engine, &io]() -> asio::awaitable<void> {
                co_await engine->shutdown();
                io.stop();
            }, asio::detached);
        });

        std::vector<std::thread> threads;
        for (std::size_t i = 1; i < config.io_threads; ++i) threads.emplace_back([&io] { io.run(); });
        io.run();
        for (auto& thread : threads) thread.join();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "gateway startup failed: " << error.what() << '\n';
        return 1;
    }
}
