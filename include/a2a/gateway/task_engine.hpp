#pragma once

#include <a2a/gateway/beast_http_client.hpp>
#include <a2a/gateway/event_store_executor.hpp>
#include <a2a/gateway/gateway_config.hpp>
#include <a2a/gateway/registry_async_client.hpp>
#include <a2a/routing/semantic_agent_router.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <json.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace a2a::gateway {

struct CreateTaskInput {
    std::string message;
    std::string skill;
    std::string tag;
};

class TaskEngine : public std::enable_shared_from_this<TaskEngine> {
public:
    TaskEngine(boost::asio::any_io_executor executor,
               GatewayConfig config,
               std::shared_ptr<EventStoreExecutor> events,
               std::shared_ptr<RegistryAsyncClient> registry,
               std::shared_ptr<routing::SemanticAgentRouter> semantic_router);

    boost::asio::awaitable<nlohmann::json> create_task(CreateTaskInput input);
    boost::asio::awaitable<std::optional<nlohmann::json>> get_task(std::string task_id);
    boost::asio::awaitable<std::optional<nlohmann::json>> stream_task(std::string task_id, long long since);
    boost::asio::awaitable<std::optional<nlohmann::json>> cancel_task(std::string task_id);
    boost::asio::awaitable<nlohmann::json> list_agents();
    boost::asio::awaitable<nlohmann::json> find_agents(std::string skill, std::string tag);
    boost::asio::awaitable<nlohmann::json> call_agent(CreateTaskInput input, int history_length = 0);
    boost::asio::awaitable<void> shutdown();

private:
    enum class State { Created, Dispatched, Running, Completed, Failed };
    struct TaskControl {
        std::shared_ptr<boost::asio::cancellation_signal> signal;
        State state = State::Created;
        bool terminal = false;
    };
    struct Selection {
        AgentDescriptor agent;
        std::string mode;
        double semantic_score = 0.0;
        double final_score = 0.0;
    };

    boost::asio::awaitable<void> run_task(CreateTaskInput input, std::string task_id,
                                          std::string trace_id, std::string context_id,
                                          std::shared_ptr<boost::asio::cancellation_signal> keep_alive);
    boost::asio::awaitable<std::optional<Selection>> select_agent(
        const CreateTaskInput& input, std::vector<AgentDescriptor> agents);
    boost::asio::awaitable<std::string> invoke_agent(const AgentDescriptor& agent,
                                                     const std::string& message,
                                                     int history_length);
    boost::asio::awaitable<bool> advance(const std::string& task_id, State state);
    boost::asio::awaitable<bool> claim_terminal(const std::string& task_id, State state);
    boost::asio::awaitable<void> erase_control(const std::string& task_id);
    void on_run_finished();
    void on_task_finished(std::exception_ptr error);
    boost::asio::awaitable<void> fail_task(const std::string& task_id, const std::string& trace_id,
                                           const std::string& context_id, nlohmann::json payload);
    static TaskEvent event(TaskEventType type, const std::string& task_id, const std::string& trace_id,
                           const std::string& context_id, const nlohmann::json& payload,
                           const std::string& target = "");
    static nlohmann::json task_view(const std::string& task_id, const std::vector<TaskEvent>& events,
                                    bool include_events, long long since = 0);

    boost::asio::any_io_executor executor_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    GatewayConfig config_;
    std::shared_ptr<EventStoreExecutor> events_;
    std::shared_ptr<RegistryAsyncClient> registry_;
    std::shared_ptr<routing::SemanticAgentRouter> semantic_router_;
    BeastHttpClient http_;
    std::unordered_map<std::string, TaskControl> controls_;
    boost::asio::steady_timer drain_timer_;
    std::size_t active_runs_ = 0;
    bool stopping_ = false;
};

} // namespace a2a::gateway
