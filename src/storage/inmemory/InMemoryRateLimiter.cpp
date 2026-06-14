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
        return {false, 0, now};
    }

    if (window.count() <= 0) {
        if (error != nullptr) {
            *error = RateLimitError::invalid_policy;
        }
        return {false, 0, now};
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
        return {false, 0, bucket.reset_at};
    }

    ++bucket.count;
    if (error != nullptr) {
        *error = RateLimitError::none;
    }
    return {true, limit - bucket.count, bucket.reset_at};
}
