#pragma once

#include "url_shortener/analytics/analytics_error.hpp"
#include "url_shortener/analytics/click_event.hpp"

namespace url_shortener::analytics {

/**
 * @brief Optional forwarding sink for click events.
 */
class IAnalyticsSink {
public:
    virtual ~IAnalyticsSink() = default;
    virtual bool Emit(const ClickEvent& event, AnalyticsError* error = nullptr) = 0;
    virtual bool Flush(AnalyticsError* error = nullptr) = 0;
};

}
