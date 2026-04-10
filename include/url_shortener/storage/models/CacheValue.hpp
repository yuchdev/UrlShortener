#pragma once

#include <chrono>
#include <optional>
#include <string>

/**
 * @brief Redirect cache payload model.
 */
struct CacheValue {
    std::string target_url;
    std::optional<std::chrono::system_clock::time_point> expires_at;
    bool is_active = true;
    std::optional<uint64_t> version;
};
