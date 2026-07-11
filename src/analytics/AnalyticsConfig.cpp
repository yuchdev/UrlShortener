#include "url_shortener/analytics/AnalyticsConfig.hpp"

namespace url_shortener::analytics {

bool AnalyticsConfig::Validate(AnalyticsError* error) const
{
    if (queue_capacity == 0 || batch_size == 0 || batch_size > queue_capacity || flush_interval.count() <= 0
        || retry.retry_backoff.count() <= 0 || retention.retention_days == 0
        || !ClickEventSanitizer::ValidateLimits(sanitization)
        || (enabled && client_hash_salt.empty())) {
        if (error) {
            error->code = AnalyticsErrorCode::validation;
            error->message = "Invalid analytics configuration";
        }
        return false;
    }
    return true;
}

} // namespace url_shortener::analytics
