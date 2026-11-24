#pragma once

#include <cstddef>
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
 * Memory bounding: expired buckets (those whose window has already
 * lapsed) are swept amortised inside `Allow` - at most once every
 * `kSweepInterval` calls, and always when `buckets_` reaches
 * `kHardCapBuckets` - so a flood of distinct keys cannot grow the map
 * without bound. A sweep costs O(size) but is amortised over at least
 * `kSweepInterval` calls, keeping `Allow` O(1) amortised. As a
 * backstop against a burst of distinct live keys outrunning the sweep,
 * a new key is only inserted after one arbitrary existing entry is
 * dropped once the map has reached `kHardCapBuckets`, so the map size
 * never exceeds that hard cap. Eviction is window-expiry based and so
 * is observationally identical to the lazy per-key reset the limiter
 * already performs; the hard-cap backstop can reset a victim key's
 * window early, which only ever makes the limiter more permissive and
 * never affects the key being evaluated.
 *
 * Lifetime: does not own `clock`; the caller must keep it alive for
 * at least as long as this object exists.
 *
 * Thread-safety: `Allow` and `bucket_count` acquire `mutex_`
 * exclusively on every call, so it is safe to use concurrently from
 * multiple threads.
 *
 * @see IRateLimiter
 */
class InMemoryRateLimiter final : public IRateLimiter
{
public:
    /**
     * @brief Minimum number of `Allow` calls between two amortised
     * sweeps of expired buckets. Smaller means tighter memory, larger
     * means cheaper per-call amortisation. Public and `constexpr` so
     * tests can drive a deterministic sweep.
     */
    static constexpr std::size_t kSweepInterval = 1024;

    /**
     * @brief Hard upper bound on the number of retained buckets. When
     * the map reaches this size a sweep runs first; if that does not
     * bring it below the cap, inserting a new key first drops one
     * existing entry, so the map never exceeds this many buckets. Acts
     * purely as a memory backstop against distinct-key floods.
     */
    static constexpr std::size_t kHardCapBuckets = 1'000'000;

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
     * request is permitted. Opportunistically sweeps expired buckets
     * (see the class note) so the bucket map stays bounded.
     * @see IRateLimiter::Allow
     * @note A `limit` of zero denies every call without setting an
     * error (the decision is `allowed = false` with `error = none`).
     */
    RateLimitDecision Allow(const std::string& key,
                            uint64_t limit,
                            std::chrono::seconds window,
                            RateLimitError* error = nullptr) override;

    /**
     * @brief Returns the number of buckets currently retained in the
     * map. Intended for tests and introspection of the eviction
     * behavior; acquires `mutex_`.
     * @return The current `buckets_` size.
     */
    std::size_t bucket_count() const;

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

    /**
     * @brief Erases every bucket whose window has already lapsed at
     * `now`. Caller must hold `mutex_`.
     * @param now Current time used to test `now >= bucket.reset_at`.
     */
    void evictExpired(std::chrono::system_clock::time_point now);

    ///< Clock used to check and set window boundaries. Not owned;
    ///< must outlive this object.
    const IClock& clock_;

    ///< Guards `buckets_` and `ops_since_sweep_` on every call.
    ///< `mutable` so the `const` `bucket_count` accessor can lock.
    mutable std::mutex mutex_;

    ///< Per-key window state; entries are created on first use and
    ///< reclaimed by `evictExpired`/the hard-cap backstop. Guarded by
    ///< `mutex_`.
    std::unordered_map<std::string, Bucket> buckets_;

    ///< `Allow` calls since the last sweep; triggers `evictExpired`
    ///< once it reaches `kSweepInterval`. Guarded by `mutex_`.
    std::size_t ops_since_sweep_ = 0;
};
