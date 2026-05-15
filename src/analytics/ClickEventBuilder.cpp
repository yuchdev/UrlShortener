#include "url_shortener/analytics/ClickEventBuilder.hpp"

#include <atomic>

namespace url_shortener::analytics {

bool ClickEventBuilder::Build(const RedirectEventContext& context, const SanitizationLimits& limits, const ClientIdHashConfig& hash_config, ClickEvent* out, AnalyticsError* error)
{
    if (!out) return false;
    static std::atomic<uint64_t> counter{1};
    ClickEvent e;
    e.event_id = "evt_" + std::to_string(counter.fetch_add(1));
    e.occurred_at = context.occurred_at;
    e.slug = context.slug;
    e.link_id = context.link_id;
    e.domain = ClickEventSanitizer::SanitizeDomain(context.domain, limits);
    e.status_code = context.final_status_code;
    e.referrer = ClickEventSanitizer::SanitizeReferrer(context.referrer, limits);
    e.user_agent = ClickEventSanitizer::SanitizeUserAgent(context.user_agent, limits);
    e.client_id_hash = ClientIdHasher::Hash(context.client_network_identifier, hash_config, error);
    if (!e.Validate(error)) return false;
    *out = std::move(e);
    return true;
}

} // namespace
