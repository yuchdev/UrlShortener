#pragma once

/**
 * @brief Analytics sink error categories.
 */
enum class AnalyticsError {
    none,
    unavailable,
    queue_full,
    permanent_failure,
};
