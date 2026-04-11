#pragma once

#include <chrono>
#include <cstdint>

/**
 * @brief Rate-limiter decision model.
 */
struct RateLimitDecision {
    bool allowed = true;
    uint64_t remaining = 0;
    std::chrono::system_clock::time_point reset_at;
};
