#pragma once

#include <ostream>

/**
 * @brief Error categories for rate-limiter operations, returned via
 * the `*error` out-param convention on `IRateLimiter::Allow`.
 * Note: a denied decision (`RateLimitDecision::allowed == false`) due
 * to a limit being reached is not an error — `none` is set in that
 * case. Only precondition and backend failures set a non-`none` code.
 */
enum class RateLimitError
{
    ///< No error; `Allow` returned a valid decision (which may still
    ///< deny the request if the limit was reached).
    none,

    ///< The caller-supplied `key` was empty; the call did not execute
    ///< and the returned decision denies the request.
    invalid_key,

    ///< The caller-supplied policy (e.g. a zero or negative `window`)
    ///< was invalid; the call did not execute and the returned
    ///< decision denies the request.
    invalid_policy,

    ///< The rate-limiter backend is temporarily unavailable; the
    ///< caller should apply its configured fail-open policy.
    unavailable,
};

/**
 * @brief Stream formatter for rate-limiter error codes.
 * @param os Stream to write to.
 * @param e Value to format.
 * @return `os`, with the enumerator's symbolic name written to it
 * (e.g. `"RateLimitError::none"`).
 */
inline std::ostream& operator<<(std::ostream& os, RateLimitError e)
{
    switch (e) {
        case RateLimitError::none:
            return os << "RateLimitError::none";
        case RateLimitError::invalid_key:
            return os << "RateLimitError::invalid_key";
        case RateLimitError::invalid_policy:
            return os << "RateLimitError::invalid_policy";
        case RateLimitError::unavailable:
            return os << "RateLimitError::unavailable";
    }
    return os << "RateLimitError(" << static_cast<int>(e) << ")";
}
