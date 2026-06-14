#pragma once

#include <a2a/server/event_store.hpp>
#include <hiredis/hiredis.h>
#include <mutex>
#include <string>

namespace a2a {

class RedisEventStore : public IEventStore {
public:
    explicit RedisEventStore(const std::string& host = "127.0.0.1", int port = 6379);
    ~RedisEventStore() override;

    RedisEventStore(const RedisEventStore&) = delete;
    RedisEventStore& operator=(const RedisEventStore&) = delete;
    RedisEventStore(RedisEventStore&&) noexcept = delete;
    RedisEventStore& operator=(RedisEventStore&&) noexcept = delete;

    void append_event(const TaskEvent& event) override;
    std::vector<TaskEvent> list_events(const std::string& task_id) override;
    std::vector<TaskEvent> list_events_by_trace(const std::string& trace_id) override;
    std::optional<TaskEvent> get_latest_event(const std::string& task_id) override;

private:
    std::string task_key(const std::string& task_id) const;
    std::string trace_key(const std::string& trace_id) const;
    void ensure_connection();
    redisReply* command(const char* format, ...);
    std::vector<TaskEvent> read_list(const std::string& key);

    redisContext* context_ = nullptr;
    std::string host_;
    int port_;
    std::mutex mutex_;
};

} // namespace a2a
