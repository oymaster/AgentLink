#include "redis_event_store.hpp"
#include <cstdarg>
#include <iostream>
#include <stdexcept>

namespace a2a {

RedisEventStore::RedisEventStore(const std::string& host, int port)
    : host_(host)
    , port_(port) {
    context_ = redisConnect(host.c_str(), port);
    if (context_ == nullptr || context_->err) {
        std::string error = context_ ? context_->errstr : "unable to allocate redis context";
        if (context_) {
            redisFree(context_);
            context_ = nullptr;
        }
        throw std::runtime_error("RedisEventStore connect failed: " + error);
    }
}

RedisEventStore::~RedisEventStore() {
    if (context_) {
        redisFree(context_);
    }
}

void RedisEventStore::append_event(const TaskEvent& event) {
    auto stored = event;
    if (stored.sequence() <= 0) {
        const auto existing = list_events(stored.task_id());
        stored.set_sequence(static_cast<long long>(existing.size() + 1));
    }

    const auto json = stored.to_json();
    auto reply = command("RPUSH %s %b", task_key(stored.task_id()).c_str(), json.data(), json.size());
    freeReplyObject(reply);

    reply = command("RPUSH %s %b", trace_key(stored.trace_id()).c_str(), json.data(), json.size());
    freeReplyObject(reply);
}

std::vector<TaskEvent> RedisEventStore::list_events(const std::string& task_id) {
    return read_list(task_key(task_id));
}

std::vector<TaskEvent> RedisEventStore::list_events_by_trace(const std::string& trace_id) {
    return read_list(trace_key(trace_id));
}

std::optional<TaskEvent> RedisEventStore::get_latest_event(const std::string& task_id) {
    auto events = list_events(task_id);
    if (events.empty()) {
        return std::nullopt;
    }
    return events.back();
}

std::string RedisEventStore::task_key(const std::string& task_id) const {
    return "a2a:events:task:" + task_id;
}

std::string RedisEventStore::trace_key(const std::string& trace_id) const {
    return "a2a:events:trace:" + trace_id;
}

void RedisEventStore::ensure_connection() {
    if (context_ && !context_->err) {
        return;
    }

    if (context_) {
        redisFree(context_);
        context_ = nullptr;
    }

    context_ = redisConnect(host_.c_str(), port_);
    if (context_ == nullptr || context_->err) {
        throw std::runtime_error("RedisEventStore reconnect failed");
    }
}

redisReply* RedisEventStore::command(const char* format, ...) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_connection();

    va_list args;
    va_start(args, format);
    auto* reply = static_cast<redisReply*>(redisvCommand(context_, format, args));
    va_end(args);

    if (reply == nullptr) {
        throw std::runtime_error("RedisEventStore command failed");
    }

    if (reply->type == REDIS_REPLY_ERROR) {
        std::string error = reply->str ? reply->str : "unknown redis error";
        freeReplyObject(reply);
        throw std::runtime_error("RedisEventStore error: " + error);
    }

    return reply;
}

std::vector<TaskEvent> RedisEventStore::read_list(const std::string& key) {
    std::vector<TaskEvent> events;
    auto* reply = command("LRANGE %s 0 -1", key.c_str());

    if (reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            const auto* item = reply->element[i];
            if (item->type != REDIS_REPLY_STRING) {
                continue;
            }

            try {
                events.push_back(TaskEvent::from_json(std::string(item->str, item->len)));
            } catch (const std::exception& e) {
                std::cerr << "[RedisEventStore] skip malformed event: " << e.what() << std::endl;
            }
        }
    }

    freeReplyObject(reply);
    return events;
}

} // namespace a2a
