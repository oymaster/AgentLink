#pragma once

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace agentlink_demo {

inline std::string require_env(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        throw std::runtime_error(
            std::string("Missing required environment variable: ") + name +
            ". Export it before running this demo."
        );
    }
    return value;
}

} // namespace agentlink_demo
