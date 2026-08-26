#pragma once

#include "url_shortener/analytics/analytics_config.hpp"
#include "url_shortener/analytics/analytics_metrics.hpp"
#include "url_shortener/analytics/click_event_builder.hpp"
#include "url_shortener/analytics/i_click_event_queue.hpp"

namespace url_shortener::analytics {

class AnalyticsService {
public:
    AnalyticsService(const AnalyticsConfig& config, IClickEventQueue& queue, IAnalyticsMetrics& metrics) noexcept : config_(config), queue_(queue), metrics_(metrics) {}
    void RecordRedirectAttempt(const RedirectEventContext& context) noexcept;
private:
    const AnalyticsConfig& config_; IClickEventQueue& queue_; IAnalyticsMetrics& metrics_;
};

}
