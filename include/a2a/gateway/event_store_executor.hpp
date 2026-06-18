#pragma once

#include <a2a/runtime/task_event.hpp>
#include <a2a/server/event_store.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/thread_pool.hpp>
#include <memory>
#include <string>
#include <vector>

namespace a2a::gateway {

class EventStoreExecutor {
public:
    EventStoreExecutor(const std::string& host, int port);
    ~EventStoreExecutor();

    boost::asio::awaitable<void> append(TaskEvent event);
    boost::asio::awaitable<std::vector<TaskEvent>> list(std::string task_id);

private:
    // This pool must remain single-threaded: one worker owns the hiredis
    // connection and serializes RedisEventStore's non-atomic sequence
    // assignment within a single Gateway process.
    boost::asio::thread_pool pool_{1};
    std::unique_ptr<IEventStore> store_;
};

} // namespace a2a::gateway
