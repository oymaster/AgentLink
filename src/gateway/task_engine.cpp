#include <a2a/gateway/task_engine.hpp>

#include <a2a/runtime/agent_router.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/system_error.hpp>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace a2a::gateway {
namespace asio = boost::asio;
using json = nlohmann::json;

namespace {

std::string make_id(const std::string& prefix) {
    thread_local std::mt19937_64 random(std::random_device{}());
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::ostringstream out;
    out << prefix << '-' << now << '-' << std::hex << random();
    return out.str();
}

json agents_json(const std::vector<AgentDescriptor>& agents) {
    json result = json::array();
    for (const auto& agent : agents) result.push_back(json::parse(agent.to_json()));
    return result;
}

} // namespace

TaskEngine::TaskEngine(asio::any_io_executor executor, GatewayConfig config,
                       std::shared_ptr<EventStoreExecutor> events,
                       std::shared_ptr<RegistryAsyncClient> registry,
                       std::shared_ptr<routing::SemanticAgentRouter> semantic_router)
    : executor_(executor), strand_(asio::make_strand(executor)), config_(std::move(config)),
      events_(std::move(events)), registry_(std::move(registry)),
      semantic_router_(std::move(semantic_router)), drain_timer_(executor) {}

TaskEvent TaskEngine::event(TaskEventType type, const std::string& task_id, const std::string& trace_id,
                            const std::string& context_id, const json& payload, const std::string& target) {
    auto value = TaskEvent::create(type, trace_id, task_id, "agentlink-gateway", target);
    value.set_context_id(context_id);
    value.set_payload_json(payload.dump());
    return value;
}

asio::awaitable<json> TaskEngine::create_task(CreateTaskInput input) {
    if (input.message.empty()) throw std::invalid_argument("message is required");
    const auto task_id = make_id("mcp-task");
    const auto trace_id = make_id("mcp-trace");
    const auto context_id = make_id("mcp-ctx");
    co_await events_->append(event(TaskEventType::Created, task_id, trace_id, context_id,
        {{"message", input.message}, {"skill", input.skill.empty() ? json(nullptr) : json(input.skill)},
         {"tag", input.tag.empty() ? json(nullptr) : json(input.tag)}}));

    co_await asio::post(strand_, asio::use_awaitable);
    if (stopping_) throw std::runtime_error("gateway is shutting down");
    auto signal = std::make_shared<asio::cancellation_signal>();
    controls_.emplace(task_id, TaskControl{signal, State::Created, false});
    ++active_runs_;
    auto self = shared_from_this();
    asio::co_spawn(executor_,
        [self, input = std::move(input), task_id, trace_id, context_id, signal]() mutable {
            return self->run_task(std::move(input), task_id, trace_id, context_id, signal);
        }, asio::bind_cancellation_slot(signal->slot(),
            [self](std::exception_ptr error) {
                self->on_task_finished(error);
            }));
    co_return json{{"taskId", task_id}, {"traceId", trace_id}, {"contextId", context_id}, {"state", "created"}};
}

asio::awaitable<bool> TaskEngine::advance(const std::string& task_id, State state) {
    co_await asio::post(strand_, asio::use_awaitable);
    const auto it = controls_.find(task_id);
    if (it == controls_.end() || it->second.terminal) co_return false;
    it->second.state = state;
    co_return true;
}

asio::awaitable<bool> TaskEngine::claim_terminal(const std::string& task_id, State state) {
    co_await asio::post(strand_, asio::use_awaitable);
    const auto it = controls_.find(task_id);
    if (it == controls_.end() || it->second.terminal) co_return false;
    it->second.terminal = true;
    it->second.state = state;
    co_return true;
}

asio::awaitable<void> TaskEngine::erase_control(const std::string& task_id) {
    co_await asio::post(strand_, asio::use_awaitable);
    controls_.erase(task_id);
}

void TaskEngine::on_run_finished() {
    auto self = shared_from_this();
    asio::dispatch(strand_, [self] {
        if (self->active_runs_ > 0) --self->active_runs_;
        if (self->stopping_ && self->active_runs_ == 0) {
            self->drain_timer_.cancel();
        }
    });
}

void TaskEngine::on_task_finished(std::exception_ptr error) {
    if (error) {
        try { std::rethrow_exception(error); }
        catch (const std::exception& exception) {
            std::cerr << "task coroutine failed: " << exception.what() << '\n';
        }
    }
    on_run_finished();
}

asio::awaitable<std::optional<TaskEngine::Selection>> TaskEngine::select_agent(
    const CreateTaskInput& input, std::vector<AgentDescriptor> agents) {
    if (!input.skill.empty()) {
        AgentRouter router(RouteStrategy::LeastLoad);
        RouteQuery query;
        query.skill = input.skill;
        query.tag = input.tag;
        auto selected = router.select(agents, query);
        if (!selected) co_return std::nullopt;
        co_return Selection{*selected, "exact", 0.0, 1.0 - std::clamp(selected->load(), 0.0, 1.0)};
    }
    auto selected = co_await semantic_router_->select(std::move(agents), input.message, input.tag);
    if (!selected) co_return std::nullopt;
    co_return Selection{selected->agent, "semantic", selected->semantic_score, selected->final_score};
}

asio::awaitable<std::string> TaskEngine::invoke_agent(const AgentDescriptor& agent,
                                                       const std::string& message,
                                                       int history_length) {
    const auto rpc = json{{"jsonrpc", "2.0"}, {"id", make_id("gateway-rpc")},
        {"method", "message/send"}, {"params", {{"message", {
            {"messageId", make_id("gateway-msg")}, {"role", "user"},
            {"parts", json::array({json{{"kind", "text"}, {"text", message}}})}}},
            {"historyLength", history_length}}}};
    const auto response = co_await http_.request("POST", agent.address(), rpc.dump(),
        std::chrono::milliseconds(config_.agent_timeout_ms));
    if (response.status < 200 || response.status >= 300) {
        throw std::runtime_error("agent HTTP " + std::to_string(response.status));
    }
    const auto document = json::parse(response.body);
    if (document.contains("error")) throw std::runtime_error(document["error"].dump());
    if (!document.contains("result") || !document["result"].contains("parts")) co_return "";
    std::string answer;
    for (const auto& part : document["result"]["parts"]) {
        if (part.value("kind", "") == "text" && part.contains("text")) {
            if (!answer.empty()) answer += '\n';
            answer += part["text"].get<std::string>();
        }
    }
    co_return answer;
}

asio::awaitable<void> TaskEngine::fail_task(const std::string& task_id, const std::string& trace_id,
                                             const std::string& context_id, json payload) {
    if (!co_await claim_terminal(task_id, State::Failed)) co_return;
    try { co_await events_->append(event(TaskEventType::Failed, task_id, trace_id, context_id, payload)); }
    catch (const std::exception& error) { std::cerr << "failed to persist task failure: " << error.what() << '\n'; }
    co_await erase_control(task_id);
}

asio::awaitable<void> TaskEngine::run_task(CreateTaskInput input, std::string task_id,
                                           std::string trace_id, std::string context_id,
                                           std::shared_ptr<asio::cancellation_signal> keep_alive) {
    (void)keep_alive;
    std::optional<json> failure;
    try {
        auto agents = co_await registry_->list_agents();
        auto selected = co_await select_agent(input, std::move(agents));
        if (!selected) {
            co_await fail_task(task_id, trace_id, context_id,
                {{"error", "agent_not_found"}, {"skill", input.skill}, {"tag", input.tag}});
            co_return;
        }
        if (!co_await advance(task_id, State::Dispatched)) co_return;
        auto dispatched = event(TaskEventType::Dispatched, task_id, trace_id, context_id,
            {{"address", selected->agent.address()}, {"agent", json::parse(selected->agent.to_json())}},
            selected->agent.agent_id());
        dispatched.set_metadata_value("route_mode", selected->mode);
        dispatched.set_metadata_value("semantic_score", std::to_string(selected->semantic_score));
        dispatched.set_metadata_value("load", std::to_string(selected->agent.load()));
        dispatched.set_metadata_value("final_score", std::to_string(selected->final_score));
        dispatched.set_metadata_value("selected_agent_id", selected->agent.agent_id());
        co_await events_->append(std::move(dispatched));
        if (!co_await advance(task_id, State::Running)) co_return;
        co_await events_->append(event(TaskEventType::Running, task_id, trace_id, context_id,
            {{"address", selected->agent.address()}}, selected->agent.agent_id()));
        const auto answer = co_await invoke_agent(selected->agent, input.message, 0);
        if (!co_await claim_terminal(task_id, State::Completed)) co_return;
        co_await events_->append(event(TaskEventType::Completed, task_id, trace_id, context_id,
            {{"answer", answer}}, selected->agent.agent_id()));
        co_await erase_control(task_id);
    } catch (const boost::system::system_error& error) {
        const bool cancelled = error.code() == asio::error::operation_aborted;
        if (!cancelled) {
            failure = json{{"error", "request_failed"}, {"message", error.what()}};
        }
    } catch (const std::exception& error) {
        failure = json{{"error", "internal_error"}, {"message", error.what()}};
    }
    if (failure) co_await fail_task(task_id, trace_id, context_id, std::move(*failure));
}

json TaskEngine::task_view(const std::string& task_id, const std::vector<TaskEvent>& events,
                           bool include_events, long long since) {
    const auto& latest = events.back();
    json view{{"taskId", task_id}, {"state", to_string(latest.type()).substr(5)},
              {"eventCount", events.size()}, {"latestEvent", to_string(latest.type())}};
    for (const auto& item : events) {
        const auto payload = json::parse(item.payload_json());
        if (item.type() == TaskEventType::Completed && payload.contains("answer")) view["answer"] = payload["answer"];
        if (item.type() == TaskEventType::Failed) view["error"] = payload;
    }
    if (include_events) {
        view["events"] = json::array();
        for (const auto& item : events) {
            if (item.sequence() > since) view["events"].push_back(json::parse(item.to_json()));
        }
    }
    return view;
}

asio::awaitable<std::optional<json>> TaskEngine::get_task(std::string task_id) {
    auto events = co_await events_->list(task_id);
    if (events.empty()) co_return std::nullopt;
    co_return task_view(task_id, events, false);
}

asio::awaitable<std::optional<json>> TaskEngine::stream_task(std::string task_id, long long since) {
    auto events = co_await events_->list(task_id);
    if (events.empty()) co_return std::nullopt;
    co_return task_view(task_id, events, true, since);
}

asio::awaitable<std::optional<json>> TaskEngine::cancel_task(std::string task_id) {
    std::shared_ptr<asio::cancellation_signal> signal;
    co_await asio::post(strand_, asio::use_awaitable);
    const auto it = controls_.find(task_id);
    if (it != controls_.end() && !it->second.terminal) {
        it->second.terminal = true;
        it->second.state = State::Failed;
        signal = it->second.signal;
    }
    if (signal) {
        signal->emit(asio::cancellation_type::total);
        auto current = co_await events_->list(task_id);
        const auto& first = current.front();
        co_await events_->append(event(TaskEventType::Failed, task_id, first.trace_id(), first.context_id(),
            {{"error", "cancelled"}, {"cancelled", true}}));
        co_await erase_control(task_id);
    }
    co_return co_await get_task(task_id);
}

asio::awaitable<json> TaskEngine::list_agents() {
    const auto agents = co_await registry_->list_agents();
    co_return json{{"success", true}, {"count", agents.size()}, {"agents", agents_json(agents)}};
}

asio::awaitable<json> TaskEngine::find_agents(std::string skill, std::string tag) {
    const auto agents = co_await registry_->list_agents();
    std::vector<AgentDescriptor> matches;
    for (const auto& agent : agents) {
        if (agent.is_healthy() && agent.has_skill(skill) && agent.has_tag(tag)) matches.push_back(agent);
    }
    co_return json{{"success", true}, {"count", matches.size()}, {"agents", agents_json(matches)}};
}

asio::awaitable<json> TaskEngine::call_agent(CreateTaskInput input, int history_length) {
    if (input.message.empty()) throw std::invalid_argument("message is required");
    auto selected = co_await select_agent(input, co_await registry_->list_agents());
    if (!selected) throw std::runtime_error("agent_not_found");
    const auto answer = co_await invoke_agent(selected->agent, input.message, history_length);
    co_return json{{"selectedAgent", json::parse(selected->agent.to_json())}, {"answer", answer}};
}

asio::awaitable<void> TaskEngine::shutdown() {
    std::vector<std::string> ids;
    co_await asio::post(strand_, asio::use_awaitable);
    stopping_ = true;
    for (const auto& [id, control] : controls_) if (!control.terminal) ids.push_back(id);
    for (const auto& id : ids) co_await cancel_task(id);
    co_await asio::post(strand_, asio::use_awaitable);
    if (active_runs_ > 0) {
        drain_timer_.expires_at(std::chrono::steady_clock::time_point::max());
        boost::system::error_code ignored;
        co_await drain_timer_.async_wait(asio::redirect_error(asio::use_awaitable, ignored));
    }
    co_return;
}

} // namespace a2a::gateway
