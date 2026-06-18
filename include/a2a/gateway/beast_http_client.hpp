#pragma once

#include <boost/asio/awaitable.hpp>
#include <chrono>
#include <map>
#include <string>

namespace a2a::gateway {

struct HttpResponse {
    unsigned status = 0;
    std::string body;
};

class BeastHttpClient {
public:
    boost::asio::awaitable<HttpResponse> request(
        const std::string& method,
        const std::string& url,
        const std::string& body,
        std::chrono::milliseconds timeout,
        std::size_t body_limit = 16 * 1024 * 1024) const;
};

} // namespace a2a::gateway
