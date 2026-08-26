#pragma once

#include <cstdint>
#include <string>

#include "url_shortener/analytics/analytics_error.hpp"
#include "url_shortener/analytics/click_event.hpp"
#include "url_shortener/analytics/click_event_sanitizer.hpp"
#include "url_shortener/analytics/client_id_hasher.hpp"

namespace url_shortener::analytics {

/**
 * @brief Input metadata for building a normalized click event.
 */
struct RedirectEventContext {
    std::string slug;
    std::string link_id;
    std::string domain;
    uint16_t final_status_code = 0;
    std::string referrer;
    std::string user_agent;
    std::string client_network_identifier;
    Timestamp occurred_at{};
};

/**
 * @brief Converts redirect context to sanitized click events.
 */
class ClickEventBuilder {
public:
    static bool Build(const RedirectEventContext& context, const SanitizationLimits& limits, const ClientIdHashConfig& hash_config, ClickEvent* out, AnalyticsError* error = nullptr);
};

} // namespace
