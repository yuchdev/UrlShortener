#pragma once

#include "url_shortener/analytics/AnalyticsConfig.hpp"
#include "url_shortener/analytics/AnalyticsMetrics.hpp"
#include "url_shortener/analytics/ClickEventBuilder.hpp"
#include "url_shortener/analytics/IClickEventQueue.hpp"

namespace url_shortener::analytics {

class AnalyticsService {
public:
    AnalyticsService(const AnalyticsConfig& config, IClickEventQueue& queue, IAnalyticsMetrics& metrics) noexcept : config_(config), queue_(queue), metrics_(metrics) {}
    void RecordRedirectAttempt(const RedirectEventContext& context) noexcept;
private:
    const AnalyticsConfig& config_; IClickEventQueue& queue_; IAnalyticsMetrics& metrics_;
};

}
