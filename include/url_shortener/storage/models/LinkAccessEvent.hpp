#pragma once

#include <chrono>
#include <optional>
#include <string>

/**
 * @brief Analytics payload emitted for redirect-resolution outcomes.
 */
struct LinkAccessEvent {
    std::string short_code;
    std::chrono::system_clock::time_point occurred_at;
    uint16_t status_code = 0;
    bool cache_hit = false;
    std::optional<std::string> client_id;
};
