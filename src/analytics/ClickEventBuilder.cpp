#include "url_shortener/analytics/ClickEventBuilder.hpp"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace url_shortener::analytics {

bool ClickEventBuilder::Build(const RedirectEventContext& context, const SanitizationLimits& limits, const ClientIdHashConfig& hash_config, ClickEvent* out, AnalyticsError* error)
{
    if (!out) return false;
    if (!ClickEventSanitizer::ValidateLimits(limits)) {
        if (error) { error->code = AnalyticsErrorCode::validation; error->message = "Invalid sanitization limits"; }
        return false;
    }
    ClickEvent e;
    thread_local static boost::uuids::random_generator uuid_gen;
    e.event_id = boost::uuids::to_string(uuid_gen());
    e.occurred_at = context.occurred_at;
    e.slug = context.slug;
    e.link_id = context.link_id;
    e.domain = ClickEventSanitizer::SanitizeDomain(context.domain, limits);
    e.status_code = context.final_status_code;
    e.referrer = ClickEventSanitizer::SanitizeReferrer(context.referrer, limits);
    e.user_agent = ClickEventSanitizer::SanitizeUserAgent(context.user_agent, limits);
    AnalyticsError hash_error;
    e.client_id_hash = ClientIdHasher::Hash(context.client_network_identifier, hash_config, &hash_error);
    if (hash_error.code != AnalyticsErrorCode::none) {
        if (error) *error = hash_error;
        return false;
    }
    if (!e.Validate(error)) return false;
    *out = std::move(e);
    return true;
}

} // namespace
