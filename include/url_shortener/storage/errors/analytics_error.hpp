#pragma once

#include <ostream>

/**
 * @brief Error categories for analytics sink operations, returned via
 * the `bool` + `*error` out-param convention on `IAnalyticsSink`
 * methods (`Emit`, `Flush`).
 */
enum class AnalyticsError
{
    ///< No error; the operation completed successfully.
    none,

    ///< The sink backend is temporarily unavailable; the event was
    ///< not persisted but the condition may resolve on retry.
    unavailable,

    ///< The event queue was full at the time of the call; the event
    ///< was silently dropped, not persisted.
    queue_full,

    ///< An unrecoverable backend error occurred; the event was not
    ///< persisted and retrying is unlikely to help.
    permanent_failure,
};

/**
 * @brief Stream formatter for analytics error codes.
 * @param os Stream to write to.
 * @param e Value to format.
 * @return `os`, with the enumerator's symbolic name written to it
 * (e.g. `"AnalyticsError::none"`).
 */
inline std::ostream& operator<<(std::ostream& os, AnalyticsError e)
{
    switch (e) {
        case AnalyticsError::none:
            return os << "AnalyticsError::none";
        case AnalyticsError::unavailable:
            return os << "AnalyticsError::unavailable";
        case AnalyticsError::queue_full:
            return os << "AnalyticsError::queue_full";
        case AnalyticsError::permanent_failure:
            return os << "AnalyticsError::permanent_failure";
    }
    return os << "AnalyticsError(" << static_cast<int>(e) << ")";
}
