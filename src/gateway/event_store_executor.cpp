#include <a2a/gateway/event_store_executor.hpp>

#include <a2a/server/redis_event_store.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace a2a::gateway {
namespace asio = boost::asio;

EventStoreExecutor::EventStoreExecutor(const std::string& host, int port)
    : store_(std::make_unique<RedisEventStore>(host, port)) {}

EventStoreExecutor::~EventStoreExecutor() {
    pool_.join();
}

asio::awaitable<void> EventStoreExecutor::append(TaskEvent event) {
    co_await asio::co_spawn(pool_, [this, event = std::move(event)]() mutable -> asio::awaitable<void> {
        store_->append_event(event);
        co_return;
    }, asio::use_awaitable);
}

asio::awaitable<std::vector<TaskEvent>> EventStoreExecutor::list(std::string task_id) {
    co_return co_await asio::co_spawn(pool_, [this, task_id = std::move(task_id)]() -> asio::awaitable<std::vector<TaskEvent>> {
        co_return store_->list_events(task_id);
    }, asio::use_awaitable);
}

} // namespace a2a::gateway
