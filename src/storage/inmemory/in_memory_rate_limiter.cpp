#include "url_shortener/storage/inmemory/in_memory_rate_limiter.hpp"

InMemoryRateLimiter::InMemoryRateLimiter(const IClock& clock)
    : clock_(clock)
{
}

void InMemoryRateLimiter::evictExpired(
    std::chrono::system_clock::time_point now)
{
    for (auto it = buckets_.begin(); it != buckets_.end();) {
        if (now >= it->second.reset_at) {
            it = buckets_.erase(it);
        }
        else {
            ++it;
        }
    }
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

    // Amortised memory bounding: sweep lapsed buckets at most once every
    // kSweepInterval calls (and always when the map hits the hard cap) so a
    // flood of distinct keys cannot grow buckets_ without bound while keeping
    // Allow O(1) amortised. Done before touching the current key's bucket:
    // an expired current key would be reset below anyway, so sweeping it is
    // observationally identical.
    if (++ops_since_sweep_ >= kSweepInterval
        || buckets_.size() >= kHardCapBuckets)
    {
        evictExpired(now);
        ops_since_sweep_ = 0;
    }

    // Hard-cap backstop: if live keys still fill the map to the cap, drop one
    // arbitrary entry before inserting a new key so buckets_ never exceeds
    // kHardCapBuckets. Evicting a victim only resets its window (more
    // permissive for that key); the key under evaluation is never the victim.
    if (buckets_.size() >= kHardCapBuckets
        && buckets_.find(key) == buckets_.end())
    {
        buckets_.erase(buckets_.begin());
    }

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

std::size_t InMemoryRateLimiter::bucket_count() const
{
    std::lock_guard lock(mutex_);
    return buckets_.size();
}
