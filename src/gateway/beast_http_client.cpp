#include <a2a/gateway/beast_http_client.hpp>
#include <a2a/version.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <stdexcept>

namespace a2a::gateway {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

namespace {

struct ParsedUrl {
    std::string host;
    std::string port;
    std::string target;
};

ParsedUrl parse_http_url(const std::string& url) {
    constexpr auto prefix = "http://";
    if (url.rfind(prefix, 0) != 0) {
        throw std::invalid_argument("only http:// URLs are supported: " + url);
    }
    const auto authority_start = std::char_traits<char>::length(prefix);
    const auto path_pos = url.find('/', authority_start);
    const auto authority = url.substr(authority_start, path_pos - authority_start);
    const auto colon = authority.rfind(':');
    ParsedUrl parsed;
    parsed.host = colon == std::string::npos ? authority : authority.substr(0, colon);
    parsed.port = colon == std::string::npos ? "80" : authority.substr(colon + 1);
    parsed.target = path_pos == std::string::npos ? "/" : url.substr(path_pos);
    if (parsed.host.empty() || parsed.port.empty()) throw std::invalid_argument("invalid URL: " + url);
    return parsed;
}

} // namespace

asio::awaitable<HttpResponse> BeastHttpClient::request(
    const std::string& method,
    const std::string& url,
    const std::string& body,
    std::chrono::milliseconds timeout,
    std::size_t body_limit) const {
    const auto parsed = parse_http_url(url);
    auto executor = co_await asio::this_coro::executor;
    tcp::resolver resolver(executor);
    beast::tcp_stream stream(executor);
    stream.expires_after(timeout);
    const auto endpoints = co_await resolver.async_resolve(parsed.host, parsed.port, asio::use_awaitable);
    co_await stream.async_connect(endpoints, asio::use_awaitable);

    const auto verb = http::string_to_verb(method);
    if (verb == http::verb::unknown) throw std::invalid_argument("unsupported HTTP method: " + method);
    http::request<http::string_body> request{verb, parsed.target, 11};
    request.set(http::field::host, parsed.host);
    request.set(http::field::user_agent, std::string(a2a::kGatewayProduct));
    request.set(http::field::accept, "application/json");
    if (!body.empty()) {
        request.set(http::field::content_type, "application/json");
        request.body() = body;
        request.prepare_payload();
    }
    co_await http::async_write(stream, request, asio::use_awaitable);

    beast::flat_buffer buffer;
    http::response_parser<http::string_body> parser;
    parser.body_limit(body_limit);
    co_await http::async_read(stream, buffer, parser, asio::use_awaitable);
    auto response = parser.release();
    beast::error_code ignored;
    stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
    co_return HttpResponse{response.result_int(), std::move(response.body())};
}

} // namespace a2a::gateway
