#pragma once

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

} // namespace url_shortener::analytics
