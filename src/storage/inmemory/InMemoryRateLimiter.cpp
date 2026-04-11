#include "url_shortener/storage/inmemory/InMemoryRateLimiter.hpp"

InMemoryRateLimiter::InMemoryRateLimiter(const IClock& clock)
    : clock_(clock)
{
}

RateLimitDecision InMemoryRateLimiter::Allow(const std::string& key,
                                             uint64_t limit,
                                             std::chrono::seconds window,
                                             RateLimitError* error)
{
    const auto now = clock_.now();
    if (key.empty()) {
        if (error != nullptr) {
            *error = RateLimitError::invalid_key;
        }
        return {.allowed = false, .remaining = 0, .reset_at = now};
    }

    if (window.count() <= 0) {
        if (error != nullptr) {
            *error = RateLimitError::invalid_policy;
        }
        return {.allowed = false, .remaining = 0, .reset_at = now};
    }

    std::lock_guard lock(mutex_);
    auto& bucket = buckets_[key];
    if (bucket.count == 0 || now >= bucket.reset_at) {
        bucket.count = 0;
        bucket.reset_at = now + window;
    }

    if (limit == 0 || bucket.count >= limit) {
        if (error != nullptr) {
            *error = RateLimitError::none;
        }
        return {.allowed = false, .remaining = 0, .reset_at = bucket.reset_at};
    }

    ++bucket.count;
    if (error != nullptr) {
        *error = RateLimitError::none;
    }
    return {.allowed = true, .remaining = limit - bucket.count, .reset_at = bucket.reset_at};
}
