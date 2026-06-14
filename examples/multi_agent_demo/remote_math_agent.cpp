#include "http_server.hpp"
#include "registry_client.hpp"
#include "redis_event_store.hpp"
#include <a2a/core/error_code.hpp>
#include <a2a/core/jsonrpc_request.hpp>
#include <a2a/core/jsonrpc_response.hpp>
#include <a2a/models/agent_message.hpp>
#include <a2a/models/message_part.hpp>
#include <a2a/runtime/task_event.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <json.hpp>
#include <regex>
#include <sstream>
#include <thread>

using json = nlohmann::json;
using namespace a2a;

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

std::string format_number(double value) {
    if (std::fabs(value - std::round(value)) < 1e-9) {
        return std::to_string(static_cast<long long>(std::llround(value)));
    }

    std::ostringstream oss;
    oss << value;
    return oss.str();
}

std::string solve_math(const std::string& text) {
    static const std::regex expr(R"(([+-]?\d+(?:\.\d+)?)\s*([+\-*/])\s*([+-]?\d+(?:\.\d+)?))");
    std::smatch match;

    if (!std::regex_search(text, match, expr)) {
        return "Only simple binary math expressions are supported, for example: 1 + 2";
    }

    double lhs = std::stod(match[1].str());
    std::string op = match[2].str();
    double rhs = std::stod(match[3].str());
    double result = 0.0;

    if (op == "+") {
        result = lhs + rhs;
    } else if (op == "-") {
        result = lhs - rhs;
    } else if (op == "*") {
        result = lhs * rhs;
    } else if (op == "/") {
        if (std::fabs(rhs) < 1e-12) {
            return "Division by zero is not allowed";
        }
        result = lhs / rhs;
    }

    return match[1].str() + " " + op + " " + match[3].str() + " = " + format_number(result);
}

std::string extract_text(const AgentMessage& message) {
    if (message.parts().empty()) {
        return "";
    }

    const auto* part = dynamic_cast<const TextPart*>(message.parts()[0].get());
    return part ? part->text() : "";
}

std::string extract_trace_id(const json& message_json) {
    if (!message_json.contains("parts") || !message_json["parts"].is_array()) {
        return "";
    }

    for (const auto& part : message_json["parts"]) {
        if (!part.is_object() || part.value("kind", "") != "data" || !part.contains("data")) {
            continue;
        }

        const auto& data = part["data"];
        if (data.is_object() && data.contains("trace_id") && data["trace_id"].is_string()) {
            return data["trace_id"].get<std::string>();
        }
    }

    return "";
}

} // namespace

class RemoteMathAgent {
public:
    RemoteMathAgent(std::string agent_id,
                       std::string listen_address,
                       std::string registry_url,
                       std::string redis_host,
                       int redis_port,
                       int step_delay_ms)
        : agent_id_(std::move(agent_id))
        , listen_address_(std::move(listen_address))
        , registry_client_(std::move(registry_url))
        , event_store_(std::move(redis_host), redis_port)
        , step_delay_ms_(step_delay_ms > 0 ? step_delay_ms : 0) {}

    void start(int port) {
        HttpServer server(port);
        server.register_handler("/", [this](const std::string& body) {
            return handle_request(body);
        });
        server.register_handler("/.well-known/agent-card.json", [this](const std::string&) {
            return agent_card();
        });

        std::thread server_thread([&server]() {
            server.start();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        register_self();
        std::cout << "[Math Agent] listening at " << listen_address_ << std::endl;

        server_thread.join();
    }

private:
    void register_self() {
        AgentSkillDescriptor skill;
        skill.name = "math";
        skill.description = "Solve simple binary math expressions";
        skill.input_modes = {"text"};
        skill.output_modes = {"text"};

        AgentRegistration registration;
        registration.id = agent_id_;
        registration.name = "Remote Math Agent";
        registration.address = listen_address_;
        registration.tags = {"math", "1.0", "remote"};
        registration.skills = {skill};
        registration.input_modes = {"text"};
        registration.output_modes = {"text"};
        registration.capabilities = {{"task_management", true}, {"streaming", false}};
        registration.version = "1.0";
        registration.platform = "remote";
        registration.runtime = "agentlink-cpp";
        registration.health = "healthy";
        registration.load = 0.1;

        if (!registry_client_.register_agent(registration)) {
            throw std::runtime_error("failed to register remote math agent");
        }

        std::cout << "[Math Agent] registered as " << agent_id_ << std::endl;
    }

    std::string handle_request(const std::string& body) {
        try {
            auto request = JsonRpcRequest::from_json(body);
            if (request.method() != "message/send") {
                return JsonRpcResponse::create_error(
                    request.id(),
                    ErrorCode::MethodNotFound,
                    "Method not found").to_json();
            }

            auto request_json = json::parse(body);
            auto message = AgentMessage::from_json(request_json["params"]["message"].dump());
            std::string user_text = extract_text(message);
            std::string trace_id = extract_trace_id(request_json["params"]["message"]);
            std::string task_id = message.task_id().value_or("remote-task-" + request.id());
            std::string context_id = message.context_id().value_or("");

            append_remote_event(
                TaskEventType::Running,
                trace_id,
                task_id,
                context_id,
                json({{"step", "received"}, {"input", user_text}}).dump());

            pause_between_steps();
            append_remote_event(
                TaskEventType::Message,
                trace_id,
                task_id,
                context_id,
                json({{"step", "parsed"}, {"message", "expression parsed"}}).dump());

            pause_between_steps();
            std::string answer = solve_math(user_text);

            append_remote_event(
                TaskEventType::Message,
                trace_id,
                task_id,
                context_id,
                json({{"step", "completed"}, {"answer", answer}}).dump());

            auto response_message = AgentMessage::create()
                .with_role(MessageRole::Agent);
            if (message.context_id().has_value()) {
                response_message.set_context_id(*message.context_id());
            }
            if (message.task_id().has_value()) {
                response_message.set_task_id(*message.task_id());
            }
            response_message.add_text_part(answer);

            std::cout << "[Math Agent] " << user_text << " -> " << answer << std::endl;
            return JsonRpcResponse::create_success(request.id(), response_message.to_json()).to_json();
        } catch (const std::exception& e) {
            std::cerr << "[Math Agent] error: " << e.what() << std::endl;
            return JsonRpcResponse::create_error("math-error", ErrorCode::InternalError, e.what()).to_json();
        }
    }

    void append_remote_event(TaskEventType type,
                             const std::string& trace_id,
                             const std::string& task_id,
                             const std::string& context_id,
                             const std::string& payload_json) {
        if (trace_id.empty() || task_id.empty()) {
            return;
        }

        auto event = TaskEvent::create(
            type,
            trace_id,
            task_id,
            agent_id_,
            "remote-orchestrator");
        event.set_context_id(context_id);
        event.set_payload_json(payload_json);
        event_store_.append_event(event);
    }

    void pause_between_steps() const {
        if (step_delay_ms_ <= 0) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(step_delay_ms_));
    }

    std::string agent_card() const {
        json card = {
            {"name", "Remote Math Agent"},
            {"description", "Deterministic remote math agent for HTTP JSON-RPC runtime demos"},
            {"version", "1.0"},
            {"capabilities", {
                {"streaming", false},
                {"push_notifications", false},
                {"task_management", true}
            }},
            {"skills", json::array({
                {
                    {"name", "math"},
                    {"description", "Solve simple binary math expressions"},
                    {"input_modes", json::array({"text"})},
                    {"output_modes", json::array({"text"})}
                }
            })},
            {"provider", {
                {"name", "AgentLink"},
                {"organization", "AgentLink"}
            }}
        };
        return card.dump();
    }

    std::string agent_id_;
    std::string listen_address_;
    RegistryClient registry_client_;
    RedisEventStore event_store_;
    int step_delay_ms_ = 0;
};

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <agent_id> <port> <registry_url>" << std::endl;
        std::cerr << "Example: " << argv[0] << " math-1 5011 http://localhost:8500" << std::endl;
        return 1;
    }

    std::string agent_id = argv[1];
    int port = std::stoi(argv[2]);
    std::string registry_url = argv[3];
    std::string listen_address = "http://localhost:" + std::to_string(port);
    std::string redis_host = argc > 4 ? argv[4] : getenv_or_default("AGENTLINK_REDIS_HOST", "127.0.0.1");
    int redis_port = argc > 5 ? std::stoi(argv[5]) : getenv_int_or_default("AGENTLINK_REDIS_PORT", 6379);
    int step_delay_ms = getenv_int_or_default("AGENTLINK_REMOTE_STEP_DELAY_MS", 0);

    try {
        RemoteMathAgent agent(agent_id, listen_address, registry_url, redis_host, redis_port, step_delay_ms);
        agent.start(port);
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
