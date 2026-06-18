#pragma once

#include <a2a/gateway/task_engine.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <memory>

namespace a2a::gateway {

class HttpServer : public std::enable_shared_from_this<HttpServer> {
public:
    HttpServer(boost::asio::any_io_executor executor, unsigned short port,
               std::shared_ptr<TaskEngine> engine);
    boost::asio::awaitable<void> run();
    void stop();

private:
    boost::asio::awaitable<void> session(boost::asio::ip::tcp::socket socket);
    boost::asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<TaskEngine> engine_;
};

} // namespace a2a::gateway
