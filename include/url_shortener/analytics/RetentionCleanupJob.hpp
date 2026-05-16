#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

#include "url_shortener/analytics/AnalyticsConfig.hpp"
#include "url_shortener/analytics/AnalyticsMetrics.hpp"
#include "url_shortener/analytics/IClickEventRepository.hpp"

namespace url_shortener::analytics {

/** @brief Result of a retention cleanup run. */
struct RetentionCleanupResult {
    bool enabled = false;
    std::optional<Timestamp> cutoff;
    std::uint64_t deleted_count = 0;
    std::optional<AnalyticsError> error;
};

/** @brief Executes retention deletion outside request/redirect hot paths. */
class RetentionCleanupJob {
public:
    using Clock = std::function<Timestamp()>;

    RetentionCleanupJob(const AnalyticsConfig& config, IClickEventRepository& repo, IAnalyticsMetrics& metrics, Clock now = {});
    RetentionCleanupResult Run() noexcept;

private:
    const AnalyticsConfig& config_;
    IClickEventRepository& repo_;
    IAnalyticsMetrics& metrics_;
    Clock now_;
};

} // namespace url_shortener::analytics
