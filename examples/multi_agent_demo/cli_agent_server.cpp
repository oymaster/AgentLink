#include "http_server.hpp"
#include "registry_client.hpp"
#include <a2a/core/error_code.hpp>
#include <a2a/core/jsonrpc_request.hpp>
#include <a2a/core/jsonrpc_response.hpp>
#include <a2a/models/agent_message.hpp>
#include <a2a/models/message_part.hpp>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <json.hpp>
#include <sstream>
#include <stdexcept>
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

std::string extract_text(const AgentMessage& message) {
    if (message.parts().empty()) {
        return "";
    }

    const auto* part = dynamic_cast<const TextPart*>(message.parts()[0].get());
    return part ? part->text() : "";
}

std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

std::string run_command_with_prompt(const std::string& command, const std::string& prompt) {
    const std::string shell_command = "printf %s " + shell_quote(prompt) + " | " + command;
    FILE* pipe = popen(shell_command.c_str(), "r");
    if (pipe == nullptr) {
        throw std::runtime_error("failed to start cli command");
    }

    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
        if (output.size() > 128 * 1024) {
            break;
        }
    }

    int status = pclose(pipe);
    if (status != 0 && output.empty()) {
        throw std::runtime_error("cli command exited with status " + std::to_string(status));
    }

    return output.empty() ? "(empty cli output)" : output;
}

bool is_mock_command(const std::string& command) {
    return command.empty() || command == "mock";
}

} // namespace

class CliAgentServer {
public:
    CliAgentServer(std::string agent_id,
                   std::string name,
                   std::string skill,
                   std::string role_prompt,
                   std::string command,
                   std::string listen_address,
                   std::string registry_url)
        : agent_id_(std::move(agent_id))
        , name_(std::move(name))
        , skill_(std::move(skill))
        , role_prompt_(std::move(role_prompt))
        , command_(std::move(command))
        , listen_address_(std::move(listen_address))
        , registry_client_(std::move(registry_url)) {}

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
        std::cout << "[CLI Agent] " << agent_id_ << " listening at " << listen_address_ << std::endl;
        std::cout << "[CLI Agent] skill=" << skill_ << " command=" << (command_.empty() ? "mock" : command_) << std::endl;

        server_thread.join();
    }

private:
    void register_self() {
        AgentSkillDescriptor skill;
        skill.name = skill_;
        skill.description = "CLI-backed role agent: " + name_;
        skill.input_modes = {"text"};
        skill.output_modes = {"text"};

        AgentRegistration registration;
        registration.id = agent_id_;
        registration.name = name_;
        registration.address = listen_address_;
        registration.tags = {"cli", "role-agent", skill_, agent_id_};
        registration.skills = {skill};
        registration.input_modes = {"text"};
        registration.output_modes = {"text"};
        registration.capabilities = {{"task_management", true}, {"streaming", false}};
        registration.version = "1.0-adapter";
        registration.platform = "local";
        registration.runtime = is_mock_command(command_) ? "agentlink-cli-mock" : "agentlink-cli-adapter";
        registration.health = "healthy";
        registration.load = 0.1;
        registration.metadata = {
            {"role_prompt", role_prompt_},
            {"command", command_.empty() ? "mock" : command_}
        };

        if (!registry_client_.register_agent(registration)) {
            throw std::runtime_error("failed to register cli agent: " + agent_id_);
        }
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
            const std::string user_text = extract_text(message);
            const std::string prompt = role_prompt_ + "\n\n任务:\n" + user_text;
            std::string answer;

            if (is_mock_command(command_)) {
                answer = "[" + agent_id_ + " / " + skill_ + "]\n角色: " + role_prompt_ + "\n处理: " + user_text;
            } else {
                answer = run_command_with_prompt(command_, prompt);
            }

            auto response_message = AgentMessage::create()
                .with_role(MessageRole::Agent);
            if (message.context_id().has_value()) {
                response_message.set_context_id(*message.context_id());
            }
            if (message.task_id().has_value()) {
                response_message.set_task_id(*message.task_id());
            }
            response_message.add_text_part(answer);

            std::cout << "[CLI Agent] " << agent_id_ << " handled task: " << user_text << std::endl;
            return JsonRpcResponse::create_success(request.id(), response_message.to_json()).to_json();
        } catch (const std::exception& e) {
            std::cerr << "[CLI Agent] error: " << e.what() << std::endl;
            return JsonRpcResponse::create_error("cli-agent-error", ErrorCode::InternalError, e.what()).to_json();
        }
    }

    std::string agent_card() const {
        json card = {
            {"name", name_},
            {"description", "Local CLI-backed AgentLink role agent prototype"},
            {"version", "1.0-adapter"},
            {"capabilities", {
                {"streaming", false},
                {"push_notifications", false},
                {"task_management", true}
            }},
            {"skills", json::array({
                {
                    {"name", skill_},
                    {"description", "Role prompt: " + role_prompt_},
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
    std::string name_;
    std::string skill_;
    std::string role_prompt_;
    std::string command_;
    std::string listen_address_;
    RegistryClient registry_client_;
};

int main(int argc, char* argv[]) {
    if (argc < 6) {
        std::cerr << "Usage: " << argv[0] << " <agent_id> <port> <registry_url> <skill> <role_prompt> [command]" << std::endl;
        std::cerr << "Example: " << argv[0] << " codex-architect 5021 http://localhost:8500 architecture \"You are an architect\" mock" << std::endl;
        return 1;
    }

    const std::string agent_id = argv[1];
    const int port = std::stoi(argv[2]);
    const std::string registry_url = argv[3];
    const std::string skill = argv[4];
    const std::string role_prompt = argv[5];
    const std::string command = argc > 6 ? argv[6] : getenv_or_default("AGENTLINK_CLI_AGENT_COMMAND", "mock");
    const std::string listen_address = "http://localhost:" + std::to_string(port);
    const std::string name = getenv_or_default("AGENTLINK_CLI_AGENT_NAME", agent_id);

    try {
        CliAgentServer agent(agent_id, name, skill, role_prompt, command, listen_address, registry_url);
        agent.start(port);
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
