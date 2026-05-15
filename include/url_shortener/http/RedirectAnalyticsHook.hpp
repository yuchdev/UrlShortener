#pragma once

#include <string>

#include "url_shortener/analytics/AnalyticsService.hpp"

namespace url_shortener::http {

struct RedirectAnalyticsInput {
    std::string slug;
    std::string link_id;
    std::string host;
    std::string referrer;
    std::string user_agent;
    std::string client_network_identifier;
    std::uint16_t status_code = 0;
};

class RedirectAnalyticsHook {
public:
    explicit RedirectAnalyticsHook(analytics::AnalyticsService& service) : service_(service) {}
    void OnRedirectOutcome(const RedirectAnalyticsInput& input) noexcept;
private:
    analytics::AnalyticsService& service_;
};

}
