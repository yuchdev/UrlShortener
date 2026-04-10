#pragma once

/**
 * @brief Rate limiter error categories.
 */
enum class RateLimitError {
    none,
    invalid_key,
    invalid_policy,
    unavailable,
};
