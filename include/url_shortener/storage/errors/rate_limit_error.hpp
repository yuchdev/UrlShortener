#pragma once

#include <ostream>

/**
 * @brief Rate limiter error categories.
 */
enum class RateLimitError {
    none,
    invalid_key,
    invalid_policy,
    unavailable,
};

inline std::ostream& operator<<(std::ostream& os, RateLimitError e)
{
    switch (e) {
    case RateLimitError::none:           return os << "RateLimitError::none";
    case RateLimitError::invalid_key:    return os << "RateLimitError::invalid_key";
    case RateLimitError::invalid_policy: return os << "RateLimitError::invalid_policy";
    case RateLimitError::unavailable:    return os << "RateLimitError::unavailable";
    }
    return os << "RateLimitError(" << static_cast<int>(e) << ")";
}
