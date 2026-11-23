#pragma once

#include <mutex>
#include <unordered_map>

#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/i_rate_limiter.hpp"

/**
 * @brief Thread-safe in-memory `IRateLimiter` using a fixed-window
 * per-key counter. Each unique `key` gets its own `Bucket`; the
 * window resets when `clock_.now() >= bucket.reset_at`. The first
 * call for a key (or any call after a window reset) starts a new
 * window of length `window` from the current time.
 *
 * Lifetime: does not own `clock`; the caller must keep it alive for
 * at least as long as this object exists.
 *
 * Thread-safety: `Allow` acquires `mutex_` exclusively on every
 * call, so it is safe to call concurrently from multiple threads.
 *
 * @see IRateLimiter
 */
class InMemoryRateLimiter final : public IRateLimiter
{
public:
    /**
     * @brief Stores a reference to `clock`; does not initialize any
     * buckets.
     * @param clock Clock used to evaluate window boundaries; must
     * outlive this object.
     */
    explicit InMemoryRateLimiter(const IClock& clock);

    /**
     * @brief Increments the per-key counter if within the limit or
     * resets it on a window rollover, then returns whether the
     * request is permitted.
     * @see IRateLimiter::Allow
     * @note A `limit` of zero denies every call without setting an
     * error (the decision is `allowed = false` with `error = none`).
     */
    RateLimitDecision Allow(const std::string& key,
                            uint64_t limit,
                            std::chrono::seconds window,
                            RateLimitError* error = nullptr) override;

private:
    /**
     * @brief Per-key rate-limit window state.
     */
    struct Bucket
    {
        ///< Number of requests counted in the current window.
        uint64_t count = 0;

        ///< Wall-clock time when the window resets; initialized to
        ///< `clock_.now() + window` on the first use or after a
        ///< rollover.
        std::chrono::system_clock::time_point reset_at;
    };

    ///< Clock used to check and set window boundaries. Not owned;
    ///< must outlive this object.
    const IClock& clock_;

    ///< Guards `buckets_` on every `Allow` call.
    std::mutex mutex_;

    ///< Per-key window state; entries are created on first use and
    ///< retained indefinitely (no eviction). Guarded by `mutex_`.
    std::unordered_map<std::string, Bucket> buckets_;
};
