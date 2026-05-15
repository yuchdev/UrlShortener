#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "url_shortener/analytics/AnalyticsError.hpp"

namespace url_shortener::analytics {

using ClickEventId = std::string;
using Timestamp = std::chrono::system_clock::time_point;

/**
 * @brief Normalized redirect-attempt analytics event.
 */
struct ClickEvent {
    ClickEventId event_id;
    Timestamp occurred_at{};
    std::string slug;
    std::string link_id;
    std::string domain;
    uint16_t status_code = 0;
    std::string referrer;
    std::string user_agent;
    std::string client_id_hash;

    bool Validate(AnalyticsError* error = nullptr) const;
};

} // namespace url_shortener::analytics
