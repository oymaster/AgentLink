#include <a2a/models/agent_message.hpp>
#include <a2a/runtime/agent_descriptor.hpp>
#include <a2a/runtime/agent_router.hpp>
#include <a2a/runtime/task_envelope.hpp>
#include <a2a/runtime/task_event.hpp>
#include <a2a/server/memory_event_store.hpp>
#include <iostream>
#include <vector>

namespace {

a2a::AgentDescriptor make_math_agent(const std::string& id,
                                     const std::string& address,
                                     double load) {
    a2a::AgentSkillDescriptor skill;
    skill.name = "math";
    skill.description = "Solve simple math tasks";
    skill.input_modes = {"text"};
    skill.output_modes = {"text"};

    a2a::AgentDescriptor descriptor;
    descriptor.set_agent_id(id);
    descriptor.set_name(id);
    descriptor.set_address(address);
    descriptor.set_version("1.0");
    descriptor.set_platform(address.find("127.0.0.1") != std::string::npos ? "local" : "remote");
    descriptor.set_runtime("agentlink-cpp");
    descriptor.set_tags({"math", descriptor.platform()});
    descriptor.set_skills({skill});
    descriptor.set_input_modes({"text"});
    descriptor.set_output_modes({"text"});
    descriptor.set_capabilities({{"streaming", false}, {"task_management", true}});
    descriptor.set_health("healthy");
    descriptor.set_load(load);
    return descriptor;
}

void append_event(a2a::MemoryEventStore& store,
                  a2a::TaskEventType type,
                  const a2a::TaskEnvelope& envelope,
                  const std::string& payload_json) {
    auto event = a2a::TaskEvent::create(
        type,
        envelope.trace_id(),
        envelope.task_id(),
        envelope.source_agent_id(),
        envelope.target_agent_id());
    event.set_context_id(envelope.context_id());
    event.set_payload_json(payload_json);
    store.append_event(event);
}

} // namespace

int main() {
    std::vector<a2a::AgentDescriptor> agents = {
        make_math_agent("local-math-agent", "http://127.0.0.1:9010", 0.2),
        make_math_agent("remote-math-agent", "https://agent.example.com/a2a", 0.4)
    };

    a2a::AgentRouter router;
    a2a::RouteQuery query;
    query.skill = "math";
    query.input_mode = "text";
    query.output_mode = "text";
    query.capability = "task_management";

    auto selected = router.select(agents, query);
    if (!selected.has_value()) {
        std::cerr << "No math agent found" << std::endl;
        return 1;
    }

    a2a::AgentMessage message = a2a::AgentMessage::create()
        .with_role(a2a::MessageRole::User)
        .with_text("1 + 2");

    auto envelope = a2a::TaskEnvelope::create(
        "orchestrator",
        selected->agent_id(),
        message,
        "message/send",
        "math.solve");
    envelope.set_metadata_value("transport", "http_jsonrpc");
    envelope.set_metadata_value("target_address", selected->address());

    a2a::MemoryEventStore events;
    append_event(events, a2a::TaskEventType::Created, envelope, "{\"input\":\"1 + 2\"}");
    append_event(events, a2a::TaskEventType::Dispatched, envelope, "{\"address\":\"" + selected->address() + "\"}");
    append_event(events, a2a::TaskEventType::Completed, envelope, "{\"answer\":\"3\"}");

    std::cout << "selected_agent=" << selected->agent_id() << std::endl;
    std::cout << "target_address=" << selected->address() << std::endl;
    std::cout << "trace_id=" << envelope.trace_id() << std::endl;
    std::cout << "task_id=" << envelope.task_id() << std::endl;
    std::cout << "event_count=" << events.list_events(envelope.task_id()).size() << std::endl;
    std::cout << "latest_event=" << a2a::to_string(events.get_latest_event(envelope.task_id())->type()) << std::endl;

    return 0;
}
