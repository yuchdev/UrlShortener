#pragma once

#include <chrono>
#include <cstddef>
#include <string>

#include "url_shortener/analytics/analytics_error.hpp"
#include "url_shortener/analytics/click_event_sanitizer.hpp"

namespace url_shortener::analytics
{

/**
 * @brief Retry behavior for analytics persistence.
 */
struct AnalyticsRetryConfig
{
    ///< Number of retries `AnalyticsWorker::PersistWithRetry` makes
    ///< after an initial failed persist attempt (so up to              
    ///< `max_retry_attempts + 1` total attempts per batch).            
    std::size_t max_retry_attempts = 3;
            

    ///< Fixed delay between retry attempts; not exponential. Must be
    ///< > 0 per `AnalyticsConfig::Validate`.
    std::chrono::milliseconds retry_backoff = std::chrono::milliseconds(100);               
};

/**
 * @brief Retention settings for analytics data lifecycle.
 */
struct AnalyticsRetentionConfig
{
    ///< Age, in days, beyond which `RetentionCleanupJob::Run` deletes
    ///< click events. Must be > 0 per `AnalyticsConfig::Validate`;
    ///< `RetentionCleanupJob` additionally treats a non-positive value
    ///< as "retention disabled" defensively.
    std::size_t retention_days = 30;
};

/**
 * @brief Analytics subsystem configuration and validation.
 */
struct AnalyticsConfig
{
    ///< Master switch for the analytics pipeline. When `false`,
    ///< `AnalyticsService::RecordRedirectAttempt` is a no-op and
    ///< `AnalyticsWorker::Start` never spawns its background thread.
    bool enabled = true;

    ///< When `false` (the default, e.g. dev/test),
    ///< `AnalyticsService` allows building a click event with no
    ///< client network identifier (passed as
    ///< `allow_missing_identifier` to `ClientIdHasher`); when
    ///< `true`, a missing identifier is a hashing failure.
    bool production_mode = false;

    ///< Maximum number of buffered, not-yet-persisted click events;
    ///< must be > 0 and >= `batch_size`.
    std::size_t queue_capacity = 8192;

    ///< Maximum number of events `AnalyticsWorker::FlushOnce` drains
    ///< and persists per batch; must be > 0 and <= `queue_capacity`.
    std::size_t batch_size = 128;

    ///< Maximum time `AnalyticsWorker::FlushOnce` waits for a batch
    ///< to fill before persisting whatever it has (including an
    ///< empty wait that yields nothing). Must be > 0.
    std::chrono::milliseconds flush_interval = std::chrono::milliseconds(1000);

    ///< Retry behavior for failed persist attempts.
    AnalyticsRetryConfig retry;

    ///< Retention/cleanup lifecycle settings.
    AnalyticsRetentionConfig retention;

    ///< Byte limits applied to click-event metadata fields
    ///< before they are stored or enqueued.
    SanitizationLimits sanitization;

    ///< HMAC-SHA256 key used by `ClientIdHasher` to
    ///< derive `ClickEvent::client_id_hash`; required
    ///< (non-empty) when `enabled` is `true`. Never log
    ///< this value (see `CLAUDE.md` secrets policy).

    std::string client_hash_salt;  

    /**
     * @brief Validates that this configuration is internally consistent
     * and safe to construct `AnalyticsService`/`AnalyticsWorker` from.
     *
     * @param[out] error If non-null and validation fails, receives
     * `AnalyticsErrorCode::validation` with a generic "Invalid analytics
     * configuration" message (the specific failed check is not
     * distinguished); left untouched on success.
     * @return `true` if all of: `queue_capacity != 0`, `batch_size != 0`,
     * `batch_size <= queue_capacity`, `flush_interval > 0`,
     * `retry.retry_backoff > 0`, `retention.retention_days != 0`,
     * `sanitization` passes `ClickEventSanitizer::ValidateLimits`, and
     * (`client_hash_salt` is non-empty whenever `enabled` is `true`) hold;
     * `false` otherwise.
     */
    bool Validate(AnalyticsError* error = nullptr) const;
};

}  // namespace url_shortener::analytics
