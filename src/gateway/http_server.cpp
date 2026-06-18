#include <a2a/gateway/http_server.hpp>
#include <a2a/version.hpp>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <charconv>
#include <iostream>
#include <stdexcept>

namespace a2a::gateway {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;
using json = nlohmann::json;

namespace {

struct Route {
    std::string path;
    std::string query;
};

Route split_target(const beast::string_view target) {
    const std::string value(target);
    const auto pos = value.find('?');
    return {value.substr(0, pos), pos == std::string::npos ? "" : value.substr(pos + 1)};
}

long long query_since(const std::string& query) {
    constexpr auto prefix = "since=";
    if (query.rfind(prefix, 0) != 0) return 0;
    long long value = 0;
    const auto text = query.substr(std::char_traits<char>::length(prefix));
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && value >= 0 ? value : 0;
}

http::response<http::string_body> make_response(http::status status, json body) {
    http::response<http::string_body> response{status, 11};
    response.set(http::field::content_type, "application/json");
    response.set(http::field::server, std::string(a2a::kGatewayProduct));
    response.keep_alive(false);
    response.body() = body.dump();
    response.prepare_payload();
    return response;
}

std::string task_id_from(const std::string& path, const std::string& suffix = "") {
    constexpr auto base = "/v1/tasks/";
    if (path.rfind(base, 0) != 0) return "";
    auto value = path.substr(std::char_traits<char>::length(base));
    if (!suffix.empty()) {
        if (value.size() <= suffix.size() || value.substr(value.size() - suffix.size()) != suffix) return "";
        value.resize(value.size() - suffix.size());
    }
    return value;
}

CreateTaskInput parse_input(const json& body) {
    if (!body.is_object() || !body.contains("message") || !body["message"].is_string()) {
        throw std::invalid_argument("message is required");
    }
    return {body["message"].get<std::string>(), body.value("skill", ""), body.value("tag", "")};
}

} // namespace

HttpServer::HttpServer(asio::any_io_executor executor, unsigned short port,
                       std::shared_ptr<TaskEngine> engine)
    : acceptor_(executor, tcp::endpoint(tcp::v4(), port)), engine_(std::move(engine)) {}

asio::awaitable<void> HttpServer::run() {
    std::cout << "Gateway listening on 0.0.0.0:" << acceptor_.local_endpoint().port() << std::endl;
    while (acceptor_.is_open()) {
        try {
            auto socket = co_await acceptor_.async_accept(asio::use_awaitable);
            asio::co_spawn(acceptor_.get_executor(), session(std::move(socket)), asio::detached);
        } catch (const boost::system::system_error& error) {
            if (error.code() != asio::error::operation_aborted && acceptor_.is_open()) {
                std::cerr << "accept failed: " << error.what() << '\n';
            }
        }
    }
}

void HttpServer::stop() {
    boost::system::error_code ignored;
    acceptor_.cancel(ignored);
    acceptor_.close(ignored);
}

asio::awaitable<void> HttpServer::session(tcp::socket socket) {
    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    parser.body_limit(1024 * 1024);
    http::response<http::string_body> response;
    try {
        co_await http::async_read(socket, buffer, parser, asio::use_awaitable);
        const auto request = parser.release();
        const auto route = split_target(request.target());
        const auto method = request.method();

        if (method == http::verb::get && route.path == "/healthz") {
            response = make_response(http::status::ok, {{"status", "ok"}});
        } else if (method == http::verb::post && route.path == "/v1/messages") {
            response = make_response(http::status::not_implemented,
                {{"error", "not_implemented"}, {"message", "/v1/messages is not implemented"}});
        } else if (method == http::verb::post && route.path == "/v1/tasks") {
            response = make_response(http::status::accepted,
                co_await engine_->create_task(parse_input(json::parse(request.body()))));
        } else if (method == http::verb::get && route.path == "/v1/agents") {
            response = make_response(http::status::ok, co_await engine_->list_agents());
        } else if (method == http::verb::post && route.path == "/v1/agents/find") {
            const auto body = json::parse(request.body());
            response = make_response(http::status::ok,
                co_await engine_->find_agents(body.value("skill", ""), body.value("tag", "")));
        } else if (method == http::verb::post && route.path == "/v1/call") {
            const auto body = json::parse(request.body());
            response = make_response(http::status::ok,
                co_await engine_->call_agent(parse_input(body), body.value("historyLength", 0)));
        } else if (method == http::verb::get && route.path.ends_with("/events")) {
            const auto id = task_id_from(route.path, "/events");
            const auto view = id.empty() ? std::nullopt : co_await engine_->stream_task(id, query_since(route.query));
            response = view ? make_response(http::status::ok, *view)
                            : make_response(http::status::not_found, {{"error", "task_not_found"}});
        } else if (method == http::verb::post && route.path.ends_with("/cancel")) {
            const auto id = task_id_from(route.path, "/cancel");
            const auto view = id.empty() ? std::nullopt : co_await engine_->cancel_task(id);
            response = view ? make_response(http::status::ok, *view)
                            : make_response(http::status::not_found, {{"error", "task_not_found"}});
        } else if (method == http::verb::get) {
            const auto id = task_id_from(route.path);
            const auto view = id.empty() ? std::nullopt : co_await engine_->get_task(id);
            response = view ? make_response(http::status::ok, *view)
                            : make_response(http::status::not_found, {{"error", "task_not_found"}});
        } else {
            response = make_response(http::status::not_found, {{"error", "not_found"}});
        }
    } catch (const std::invalid_argument& error) {
        response = make_response(http::status::bad_request, {{"error", "invalid_request"}, {"message", error.what()}});
    } catch (const nlohmann::json::exception& error) {
        response = make_response(http::status::bad_request, {{"error", "invalid_json"}, {"message", error.what()}});
    } catch (const std::exception& error) {
        const std::string message = error.what();
        const auto unavailable = message.find("Redis") != std::string::npos;
        const auto agent_missing = message == "agent_not_found";
        response = make_response(unavailable ? http::status::service_unavailable : http::status::bad_gateway,
            {{"error", unavailable ? "service_unavailable" : agent_missing ? "agent_not_found" : "upstream_error"},
             {"message", message}});
    }
    co_await http::async_write(socket, response, asio::use_awaitable);
    boost::system::error_code ignored;
    socket.shutdown(tcp::socket::shutdown_both, ignored);
}

} // namespace a2a::gateway
