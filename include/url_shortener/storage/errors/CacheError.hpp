#pragma once

#include <ostream>

/**
 * @brief Cache store error categories.
 */
enum class CacheError {
    none,
    unavailable,
    invalid_argument,
    permanent_failure,
};

inline std::ostream& operator<<(std::ostream& os, CacheError e)
{
    switch (e) {
    case CacheError::none:              return os << "CacheError::none";
    case CacheError::unavailable:       return os << "CacheError::unavailable";
    case CacheError::invalid_argument:  return os << "CacheError::invalid_argument";
    case CacheError::permanent_failure: return os << "CacheError::permanent_failure";
    }
    return os << "CacheError(" << static_cast<int>(e) << ")";
}
