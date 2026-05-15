#pragma once

#include <chrono>
#include <cstddef>
#include <string>

#include "url_shortener/analytics/AnalyticsError.hpp"
#include "url_shortener/analytics/ClickEventSanitizer.hpp"

namespace url_shortener::analytics {

/**
 * @brief Retry behavior for analytics persistence.
 */
struct AnalyticsRetryConfig {
    std::size_t max_retry_attempts = 3;
    std::chrono::milliseconds retry_backoff = std::chrono::milliseconds(100);
};

/**
 * @brief Retention settings for analytics data lifecycle.
 */
struct AnalyticsRetentionConfig {
    std::size_t retention_days = 30;
};

/**
 * @brief Analytics subsystem configuration and validation.
 */
struct AnalyticsConfig {
    bool enabled = true;
    bool production_mode = false;
    std::size_t queue_capacity = 8192;
    std::size_t batch_size = 128;
    std::chrono::milliseconds flush_interval = std::chrono::milliseconds(1000);
    AnalyticsRetryConfig retry;
    AnalyticsRetentionConfig retention;
    SanitizationLimits sanitization;
    std::string client_hash_salt;

    bool Validate(AnalyticsError* error = nullptr) const;
};

} // namespace url_shortener::analytics
