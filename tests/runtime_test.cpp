#include <a2a/models/agent_message.hpp>
#include <a2a/runtime/agent_descriptor.hpp>
#include <a2a/runtime/agent_router.hpp>
#include <a2a/runtime/task_envelope.hpp>
#include <a2a/runtime/task_event.hpp>
#include <a2a/server/memory_event_store.hpp>
#include <cassert>
#include <iostream>

namespace {

a2a::AgentDescriptor make_agent(const std::string& id,
                                const std::string& skill_name,
                                const std::string& health,
                                double load) {
    a2a::AgentSkillDescriptor skill;
    skill.name = skill_name;
    skill.description = "test skill";
    skill.input_modes = {"text"};
    skill.output_modes = {"text"};

    a2a::AgentDescriptor agent;
    agent.set_agent_id(id);
    agent.set_name(id);
    agent.set_address("http://127.0.0.1/" + id);
    agent.set_tags({"test", skill_name});
    agent.set_skills({skill});
    agent.set_input_modes({"text"});
    agent.set_output_modes({"text"});
    agent.set_capabilities({{"task_management", true}});
    agent.set_health(health);
    agent.set_load(load);
    return agent;
}

void test_task_envelope_roundtrip() {
    auto message = a2a::AgentMessage::create()
        .with_role(a2a::MessageRole::User)
        .with_text("hello");

    auto envelope = a2a::TaskEnvelope::create("agent-a", "agent-b", message, "message/send", "echo");
    envelope.set_parent_task_id("parent-1");
    envelope.set_metadata_value("transport", "http_jsonrpc");

    const auto parsed = a2a::TaskEnvelope::from_json(envelope.to_json());
    assert(parsed.trace_id() == envelope.trace_id());
    assert(parsed.task_id() == envelope.task_id());
    assert(parsed.context_id() == envelope.context_id());
    assert(parsed.parent_task_id() == "parent-1");
    assert(parsed.source_agent_id() == "agent-a");
    assert(parsed.target_agent_id() == "agent-b");
    assert(parsed.method() == "message/send");
    assert(parsed.intent() == "echo");
    assert(parsed.message().get_text() == "hello");
    assert(parsed.metadata().at("transport") == "http_jsonrpc");
}

void test_task_event_store() {
    a2a::MemoryEventStore store;

    auto created = a2a::TaskEvent::create(a2a::TaskEventType::Created, "trace-1", "task-1", "a", "b");
    created.set_payload_json("{\"state\":\"created\"}");
    auto completed = a2a::TaskEvent::create(a2a::TaskEventType::Completed, "trace-1", "task-1", "b", "a");

    store.append_event(created);
    store.append_event(completed);

    const auto by_task = store.list_events("task-1");
    const auto by_trace = store.list_events_by_trace("trace-1");
    const auto latest = store.get_latest_event("task-1");

    assert(by_task.size() == 2);
    assert(by_trace.size() == 2);
    assert(by_task[0].sequence() == 1);
    assert(by_task[1].sequence() == 2);
    assert(latest.has_value());
    assert(latest->type() == a2a::TaskEventType::Completed);

    const auto parsed = a2a::TaskEvent::from_json(created.to_json());
    assert(parsed.payload_json() == "{\"state\":\"created\"}");
}

void test_agent_descriptor_and_router() {
    auto busy = make_agent("busy", "math", "healthy", 0.9);
    auto idle = make_agent("idle", "math", "healthy", 0.1);
    auto offline = make_agent("offline", "math", "offline", 0.0);

    auto parsed = a2a::AgentDescriptor::from_json(idle.to_json());
    assert(parsed.agent_id() == "idle");
    assert(parsed.has_skill("math"));
    assert(parsed.has_tag("test"));
    assert(parsed.supports_input_mode("text"));
    assert(parsed.capability_enabled("task_management"));

    a2a::AgentRouter router;
    a2a::RouteQuery query;
    query.skill = "math";
    query.input_mode = "text";
    query.output_mode = "text";
    query.capability = "task_management";

    const auto selected = router.select({busy, idle, offline}, query);
    assert(selected.has_value());
    assert(selected->agent_id() == "idle");

    query.skill = "translation";
    assert(!router.select({busy, idle, offline}, query).has_value());
}

} // namespace

int main() {
    test_task_envelope_roundtrip();
    test_task_event_store();
    test_agent_descriptor_and_router();

    std::cout << "runtime_test passed" << std::endl;
    return 0;
}
