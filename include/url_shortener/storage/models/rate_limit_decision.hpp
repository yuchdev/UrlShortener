#pragma once

#include <chrono>
#include <cstdint>

/**
 * @brief Result of one `IRateLimiter::Allow` call; describes whether
 * the request is permitted and how many calls remain in the current
 * window.
 */
struct RateLimitDecision
{
    ///< `true` when the request is permitted under the current
    ///< policy; `false` when the limit is exceeded or a precondition
    ///< failed (see `RateLimitError` for the distinction).
    bool allowed = true;

    ///< Number of requests still permitted in the current window
    ///< after this call; zero when `allowed` is `false`.
    uint64_t remaining = 0;

    ///< Wall-clock time when the rate-limit window resets and the
    ///< counter starts over; meaningful even when `allowed` is
    ///< `false` so callers can report a retry-after delay.
    std::chrono::system_clock::time_point reset_at;
};
