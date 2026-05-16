#include "url_shortener/analytics/RetentionCleanupJob.hpp"

namespace url_shortener::analytics {

RetentionCleanupJob::RetentionCleanupJob(const AnalyticsConfig& config, IClickEventRepository& repo, IAnalyticsMetrics& metrics, Clock now)
    : config_(config), repo_(repo), metrics_(metrics), now_(now ? std::move(now) : [] { return std::chrono::system_clock::now(); })
{
}

RetentionCleanupResult RetentionCleanupJob::Run() noexcept
{
    RetentionCleanupResult result;
    const auto retention_days = static_cast<long long>(config_.retention.retention_days);
    if (retention_days <= 0) {
        result.enabled = false;
        return result;
    }

    result.enabled = true;
    const auto cutoff = now_() - std::chrono::hours(24 * retention_days);
    result.cutoff = cutoff;

    AnalyticsError error;
    if (!repo_.DeleteOlderThan(cutoff, &error)) {
        result.error = error;
        metrics_.OnWorkerFailure();
        return result;
    }

    result.deleted_count = 0;
    return result;
}

} // namespace url_shortener::analytics
