#pragma once

#include <ostream>

/**
 * @brief Analytics sink error categories.
 */
enum class AnalyticsError {
    none,
    unavailable,
    queue_full,
    permanent_failure,
};

inline std::ostream& operator<<(std::ostream& os, AnalyticsError e)
{
    switch (e) {
    case AnalyticsError::none:              return os << "AnalyticsError::none";
    case AnalyticsError::unavailable:       return os << "AnalyticsError::unavailable";
    case AnalyticsError::queue_full:        return os << "AnalyticsError::queue_full";
    case AnalyticsError::permanent_failure: return os << "AnalyticsError::permanent_failure";
    }
    return os << "AnalyticsError(" << static_cast<int>(e) << ")";
}
