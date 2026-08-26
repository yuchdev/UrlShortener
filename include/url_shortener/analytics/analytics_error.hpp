#pragma once

#include <ostream>
#include <string>

namespace url_shortener::analytics {

/**
 * @brief Error categories for analytics domain operations.
 */
enum class AnalyticsErrorCode {
    none,
    validation,
    queue,
    repository,
    worker,
    retention,
};

/**
 * @brief Domain error model used by analytics components.
 */
struct AnalyticsError {
    AnalyticsErrorCode code = AnalyticsErrorCode::none;
    std::string message;
};

/**
 * @brief Stream formatter for analytics error codes.
 */
inline std::ostream& operator<<(std::ostream& os, AnalyticsErrorCode code)
{
    switch (code) {
    case AnalyticsErrorCode::none:       return os << "AnalyticsErrorCode::none";
    case AnalyticsErrorCode::validation: return os << "AnalyticsErrorCode::validation";
    case AnalyticsErrorCode::queue:      return os << "AnalyticsErrorCode::queue";
    case AnalyticsErrorCode::repository: return os << "AnalyticsErrorCode::repository";
    case AnalyticsErrorCode::worker:     return os << "AnalyticsErrorCode::worker";
    case AnalyticsErrorCode::retention:  return os << "AnalyticsErrorCode::retention";
    }
    return os << "AnalyticsErrorCode(" << static_cast<int>(code) << ")";
}

/**
 * @brief Stream formatter for analytics error values.
 */
inline std::ostream& operator<<(std::ostream& os, const AnalyticsError& error)
{
    return os << "AnalyticsError{code=" << error.code << ", message='" << error.message << "'}";
}

} // namespace url_shortener::analytics
