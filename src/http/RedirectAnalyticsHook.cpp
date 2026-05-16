#include "url_shortener/http/RedirectAnalyticsHook.hpp"

namespace url_shortener::http {
void RedirectAnalyticsHook::OnRedirectOutcome(const RedirectAnalyticsInput& input) noexcept {
    analytics::RedirectEventContext ctx;
    ctx.slug = input.slug;
    ctx.link_id = input.link_id;
    ctx.domain = input.host;
    ctx.referrer = input.referrer;
    ctx.user_agent = input.user_agent;
    ctx.client_network_identifier = input.client_network_identifier;
    ctx.final_status_code = input.status_code;
    service_.RecordRedirectAttempt(ctx);
}
}
