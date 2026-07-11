#include "url_shortener/http/RedirectAnalyticsHook.hpp"

#include <chrono>

namespace url_shortener::http {
void RedirectAnalyticsHook::OnRedirectOutcome(const RedirectAnalyticsInput& input) noexcept {
    analytics::RedirectEventContext ctx;
    ctx.slug = input.slug;
    ctx.link_id = input.link_id;
    // Strip optional ":port" suffix from Host header value.
    // For IPv6 addresses like [::1]:8080 the closing bracket comes before the port.
    const std::string& h = input.host;
    if (!h.empty() && h.back() != ']') {
        auto colon = h.rfind(':');
        ctx.domain = (colon != std::string::npos) ? h.substr(0, colon) : h;
    } else {
        ctx.domain = h;
    }
    ctx.referrer = input.referrer;
    ctx.user_agent = input.user_agent;
    ctx.client_network_identifier = input.client_network_identifier;
    ctx.final_status_code = input.status_code;
    ctx.occurred_at = std::chrono::system_clock::now();
    service_.RecordRedirectAttempt(ctx);
}
}
