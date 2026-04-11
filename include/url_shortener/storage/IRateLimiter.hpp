#pragma once

#include <chrono>
#include <string>

#include "url_shortener/storage/errors/RateLimitError.hpp"
#include "url_shortener/storage/models/RateLimitDecision.hpp"

/**
 * @brief Rate-limiting abstraction for optional request throttling.
 */
class IRateLimiter {
public:
    virtual ~IRateLimiter() = default;

    /**
     * @brief Evaluates whether a key can proceed under configured policy.
     */
    virtual RateLimitDecision Allow(const std::string& key,
                                    uint64_t limit,
                                    std::chrono::seconds window,
                                    RateLimitError* error = nullptr) = 0;
};
