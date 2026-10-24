#pragma once

#include <cstdint>
#include <string>

#include "url_shortener/analytics/analytics_error.hpp"
#include "url_shortener/analytics/click_event.hpp"
#include "url_shortener/analytics/click_event_sanitizer.hpp"
#include "url_shortener/analytics/client_id_hasher.hpp"

namespace url_shortener::analytics
{

/**
 * @brief Input metadata for building a normalized click event. Populated
 * by the redirect handler at the point a redirect response has been
 * decided, and consumed by `ClickEventBuilder::Build`.
 */
struct RedirectEventContext
{
    ///< Short-link slug that was resolved.
    std::string slug;

    ///< Stable identifier of the resolved link.
    std::string link_id;

    ///< Raw `Host` header of the inbound request; sanitized by
    ///< `ClickEventBuilder::Build` before use.
    std::string domain;

    ///< HTTP status code the redirect responded with.
    uint16_t final_status_code = 0;

    ///< Raw `Referer` header value, if present; sanitized by
    ///< `ClickEventBuilder::Build` before use.
    std::string referrer;

    ///< Raw `User-Agent` header value, if present; sanitized
    ///< by `ClickEventBuilder::Build` before use.
    std::string user_agent;

    ///< Raw, not-yet-hashed client identifier (e.g. the caller's IP).
    ///< Hashed via `ClientIdHasher` before it ever reaches a `ClickEvent`;
    ///< the raw value here must never be logged or persisted as-is.
    std::string client_network_identifier;

    ///< Wall-clock time the redirect attempt completed.
    Timestamp occurred_at {};
};

/**
 * @brief Converts redirect context to sanitized, validated click events.
 */
class ClickEventBuilder
{
public:
    /**
     * @brief Builds a `ClickEvent` from `context`: sanitizes
     * `domain`/`referrer`/`user_agent` per `limits`, hashes
     * `client_network_identifier` per `hash_config`, assigns a fresh
     * random UUID as `event_id`, and validates the result.
     * 
     * @param context Redirect metadata to normalize; not retained.
     * @param limits Byte limits applied to `domain`/`referrer`/
     * `user_agent`; must itself pass `ClickEventSanitizer::ValidateLimits`.
     * @param hash_config Configuration for hashing
     * `context.client_network_identifier` (see `ClientIdHasher::Hash`).
     * @param out Receives the built event on success; left in a
     * partially-constructed, unspecified state on failure - callers must
     * not use `*out` unless this returns `true`.
     * @param[out] error If non-null and the call fails, receives the
     * failure reason: `AnalyticsErrorCode::validation` if `limits` is
     * invalid, the `ClientIdHasher::Hash` error if hashing fails (e.g.
     * missing salt, or a missing identifier that `hash_config` does not
     * allow), or the `ClickEvent::Validate` error if the assembled event
     * fails validation (e.g. an unsupported `final_status_code`). Left
     * untouched on success.
     * @return `true` if `out` is non-null and every step above succeeded;
     * `false` otherwise.
     */
    static bool Build(const RedirectEventContext& context,
                      const SanitizationLimits& limits,
                      const ClientIdHashConfig& hash_config,
                      ClickEvent* out,
                      AnalyticsError* error = nullptr);
};

}  // namespace url_shortener::analytics
