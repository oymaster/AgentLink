#include "redis_event_store.hpp"
#include "registry_client.hpp"
#include <a2a/client/a2a_client.hpp>
#include <a2a/models/agent_message.hpp>
#include <a2a/models/message_part.hpp>
#include <a2a/models/message_send_params.hpp>
#include <a2a/runtime/agent_router.hpp>
#include <a2a/runtime/task_envelope.hpp>
#include <a2a/runtime/task_event.hpp>
#include <a2a/server/event_store.hpp>
#include <chrono>
#include <cstdlib>
#include <cstddef>
#include <future>
#include <iostream>
#include <json.hpp>
#include <stdexcept>
#include <vector>

using namespace a2a;
using json = nlohmann::json;

namespace {

std::string getenv_or_default(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return fallback;
    }
    return value;
}

int getenv_int_or_default(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return fallback;
    }

    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        return fallback;
    }
}

std::string extract_text(const AgentMessage& message) {
    if (message.parts().empty()) {
        return "";
    }

    const auto* part = dynamic_cast<const TextPart*>(message.parts()[0].get());
    return part ? part->text() : "";
}

void append_event(IEventStore& store,
                  TaskEventType type,
                  const TaskEnvelope& envelope,
                  const std::string& payload_json) {
    auto event = TaskEvent::create(
        type,
        envelope.trace_id(),
        envelope.task_id(),
        envelope.source_agent_id(),
        envelope.target_agent_id());
    event.set_context_id(envelope.context_id());
    event.set_payload_json(payload_json);
    store.append_event(event);
}

std::vector<AgentDescriptor> descriptors_from_registrations(const std::vector<AgentRegistration>& registrations) {
    std::vector<AgentDescriptor> descriptors;
    for (const auto& registration : registrations) {
        descriptors.push_back(registration.to_descriptor());
    }
    return descriptors;
}

void print_new_events(const std::vector<TaskEvent>& events, std::size_t& seen_count) {
    for (std::size_t i = seen_count; i < events.size(); ++i) {
        const auto& event = events[i];
        std::cout << "stream_event=" << to_string(event.type())
                  << " sequence=" << event.sequence()
                  << " payload=" << event.payload_json()
                  << std::endl;
    }
    seen_count = events.size();
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <registry_url> [query]" << std::endl;
        std::cerr << "Example: " << argv[0] << " http://localhost:8500 \"21 * 2\"" << std::endl;
        return 1;
    }

    const std::string registry_url = argv[1];
    const std::string query = argc > 2 ? argv[2] : "1 + 2";

    try {
        RegistryClient registry(registry_url);
        auto registrations = registry.find_agents_by_skill("math");
        auto descriptors = descriptors_from_registrations(registrations);

        AgentRouter router;
        RouteQuery route_query;
        route_query.skill = "math";
        route_query.input_mode = "text";
        route_query.output_mode = "text";
        route_query.capability = "task_management";

        auto selected = router.select(descriptors, route_query);
        if (!selected.has_value()) {
            throw std::runtime_error("no healthy math agent found from registry");
        }

        auto message = AgentMessage::create()
            .with_role(MessageRole::User)
            .with_text(query);
        auto envelope = TaskEnvelope::create(
            "remote-orchestrator",
            selected->agent_id(),
            message,
            "message/send",
            "math.solve");
        envelope.set_metadata_value("transport", "http_jsonrpc");
        envelope.set_metadata_value("target_address", selected->address());

        auto routed_message = envelope.message();
        routed_message.add_data_part(json({
            {"trace_id", envelope.trace_id()},
            {"source_agent_id", envelope.source_agent_id()}
        }).dump());
        envelope.set_message(routed_message);

        const auto redis_host = getenv_or_default("AGENTLINK_REDIS_HOST", "127.0.0.1");
        const auto redis_port = getenv_int_or_default("AGENTLINK_REDIS_PORT", 6379);
        const auto configured_poll_ms = getenv_int_or_default("AGENTLINK_EVENT_POLL_MS", 200);
        const auto poll_ms = configured_poll_ms > 0 ? configured_poll_ms : 200;
        RedisEventStore events(redis_host, redis_port);
        append_event(events, TaskEventType::Created, envelope, json({{"query", query}}).dump());

        MessageSendParams params(envelope.message());
        params.set_context_id(envelope.context_id());
        params.set_task_id(envelope.task_id());
        params.set_history_length(0);

        append_event(events, TaskEventType::Dispatched, envelope, json({{"address", selected->address()}}).dump());

        std::size_t seen_event_count = 0;
        print_new_events(events.list_events(envelope.task_id()), seen_event_count);

        const auto target_address = selected->address();
        auto response_future = std::async(std::launch::async, [target_address, params]() {
            A2AClient client(target_address);
            client.set_timeout(5);
            return client.send_message(params);
        });

        while (response_future.wait_for(std::chrono::milliseconds(poll_ms)) != std::future_status::ready) {
            print_new_events(events.list_events(envelope.task_id()), seen_event_count);
        }

        auto response = response_future.get();
        if (!response.is_message()) {
            throw std::runtime_error("remote math agent returned a task, expected message");
        }

        const auto& response_message = response.as_message();
        std::string answer = extract_text(response_message);
        append_event(events, TaskEventType::Completed, envelope, json({{"answer", answer}}).dump());

        const auto persisted_events = events.list_events(envelope.task_id());
        print_new_events(persisted_events, seen_event_count);
        const auto latest_event = events.get_latest_event(envelope.task_id());

        std::cout << "selected_agent=" << selected->agent_id() << std::endl;
        std::cout << "target_address=" << selected->address() << std::endl;
        std::cout << "redis_host=" << redis_host << std::endl;
        std::cout << "redis_port=" << redis_port << std::endl;
        std::cout << "event_poll_ms=" << poll_ms << std::endl;
        std::cout << "trace_id=" << envelope.trace_id() << std::endl;
        std::cout << "task_id=" << envelope.task_id() << std::endl;
        std::cout << "answer=" << answer << std::endl;
        std::cout << "event_count=" << persisted_events.size() << std::endl;
        if (latest_event.has_value()) {
            std::cout << "latest_event=" << to_string(latest_event->type()) << std::endl;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "remote_orchestrator_error=" << e.what() << std::endl;
        return 1;
    }
}
