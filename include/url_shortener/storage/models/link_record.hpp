#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

/**
 * @brief Canonical persisted short-link metadata record.
 */
struct LinkRecord {
    std::string id;
    std::string short_code;
    std::string target_url;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    std::optional<std::chrono::system_clock::time_point> expires_at;
    std::optional<std::chrono::system_clock::time_point> deleted_at;
    std::optional<std::string> owner;
    std::map<std::string, std::string> attributes;
    bool is_active = true;
    uint64_t version = 1;
};
