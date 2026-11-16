#define BOOST_TEST_MODULE rl_eviction
#include <string>

#include <boost/test/unit_test.hpp>

#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/inmemory/in_memory_rate_limiter.hpp"

// Type: unit (in-memory rate limiter, bucket eviction / memory bounding).
// Scenario: many distinct keys populate the map, the clock advances past the
//   window, and continued activity triggers an amortised sweep that drops the
//   now-expired buckets so the map shrinks back toward the live working set.
// Boundaries: eviction is window-expiry based, so a swept bucket is identical
//   to the lazy per-key reset; the sweep fires once ops reach kSweepInterval.
// On failure first check: did the sweep stop running inside Allow, or did
//   evictExpired stop erasing lapsed buckets?
BOOST_AUTO_TEST_CASE(expired_buckets_are_swept_and_map_shrinks)
{
    ManualClock clock(std::chrono::system_clock::time_point {});
    InMemoryRateLimiter limiter(clock);
    RateLimitError error = RateLimitError::none;

    // Populate many distinct keys within one window.
    constexpr std::size_t key_count = 5000;
    for (std::size_t i = 0; i < key_count; ++i) {
        limiter.Allow(
            "peer-" + std::to_string(i), 10, std::chrono::seconds(10), &error);
    }
    BOOST_TEST(limiter.bucket_count() >= key_count);

    // Let every populated window lapse.
    clock.advance(std::chrono::seconds(11));

    // Drive enough activity on a single live key to force one amortised sweep;
    // the 5000 expired buckets must be reclaimed, leaving only the live key.
    for (std::size_t i = 0; i <= InMemoryRateLimiter::kSweepInterval; ++i) {
        limiter.Allow("sentinel", 1'000'000, std::chrono::seconds(10), &error);
    }

    BOOST_TEST(limiter.bucket_count() < key_count);
    BOOST_TEST(limiter.bucket_count() <= 1u);
}

// Type: unit (in-memory rate limiter, eviction preserves semantics).
// Scenario: a key that is still inside its window survives a sweep triggered by
//   unrelated traffic, so its counter keeps accumulating and the limit still
//   applies.
// Boundaries: only lapsed buckets are swept; a live bucket must not be dropped.
// On failure first check: did evictExpired start erasing live buckets?
BOOST_AUTO_TEST_CASE(live_bucket_survives_sweep)
{
    ManualClock clock(std::chrono::system_clock::time_point {});
    InMemoryRateLimiter limiter(clock);
    RateLimitError error = RateLimitError::none;

    // Spend the one allowed request for "live" at t=0 (window 100s).
    BOOST_TEST(
        limiter.Allow("live", 1, std::chrono::seconds(100), &error).allowed);

    // Churn other short-lived keys and advance just past their window so a
    // sweep has expired victims, while "live" is still well inside its window.
    for (std::size_t i = 0; i <= InMemoryRateLimiter::kSweepInterval; ++i) {
        limiter.Allow(
            "churn-" + std::to_string(i), 1, std::chrono::seconds(1), &error);
    }
    clock.advance(std::chrono::seconds(2));
    for (std::size_t i = 0; i <= InMemoryRateLimiter::kSweepInterval; ++i) {
        limiter.Allow(
            "churn2-" + std::to_string(i), 1, std::chrono::seconds(1), &error);
    }

    // "live" is still within its 100s window (only 2s elapsed): its single
    // allowance remains spent, so it stays denied - it was not reset by a
    // sweep.
    const auto decision =
        limiter.Allow("live", 1, std::chrono::seconds(100), &error);
    BOOST_TEST(!decision.allowed);
}
