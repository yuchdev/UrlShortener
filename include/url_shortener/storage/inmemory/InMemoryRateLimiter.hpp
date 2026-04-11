#pragma once

#include <mutex>
#include <unordered_map>

#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/IRateLimiter.hpp"

/**
 * @brief Fixed-window in-memory rate limiter implementation.
 */
class InMemoryRateLimiter final : public IRateLimiter {
public:
    explicit InMemoryRateLimiter(const IClock& clock);

    RateLimitDecision Allow(const std::string& key,
                            uint64_t limit,
                            std::chrono::seconds window,
                            RateLimitError* error = nullptr) override;

private:
    struct Bucket {
        uint64_t count = 0;
        std::chrono::system_clock::time_point reset_at;
    };

    const IClock& clock_;
    std::mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
};
