#pragma once
#include <string>

#include "url_shortener/analytics/IClickEventRepository.hpp"

namespace url_shortener::http {

class AnalyticsStatsHandler {
public:
    explicit AnalyticsStatsHandler(analytics::IClickEventRepository& repo) : repo_(repo) {}
    bool Handle(const std::string& slug, const std::string& from, const std::string& to, const std::string& bucket,
                std::string* body, int* status_code, std::string request_id = "") const;
private:
    analytics::IClickEventRepository& repo_;
};

}
