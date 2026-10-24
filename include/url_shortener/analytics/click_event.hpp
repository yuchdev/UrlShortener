#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "url_shortener/analytics/analytics_error.hpp"

namespace url_shortener::analytics
{

/**
 * @brief Opaque identifier for a `ClickEvent`. Currently a random UUID
 * string assigned by `ClickEventBuilder::Build` (via
 * `boost::uuids::random_generator`); callers must not assume any other
 * format, and this type does not validate it.
 */
using ClickEventId = std::string;

/**
 * @brief Wall-clock time point used throughout the analytics domain (e.g.
 * `ClickEvent::occurred_at`, `AggregateQuery::from`/`to`, retention
 * cutoffs). An alias of `std::chrono::system_clock::time_point`.
 */
using Timestamp = std::chrono::system_clock::time_point;

/**
 * @brief Normalized redirect-attempt analytics event, ready to enqueue or
 * persist. Produced by `ClickEventBuilder::Build`, which assigns
 * `event_id`, applies sanitization limits to `domain`/`referrer`/
 * `user_agent`, and hashes the raw client identifier into
 * `client_id_hash` before any `ClickEvent` is constructed.
 */
struct ClickEvent
{
    ///< See `ClickEventId`; must be non-empty per `Validate`.
    ClickEventId event_id;

    ///< Wall-clock time the redirect attempt completed;
    ///< must be set to a non-default value per `Validate`.
    Timestamp occurred_at {};  

    ///< Short-link slug that was resolved (without the `/r/`
    ///< prefix); must be non-empty per `Validate`.
    std::string slug;

    ///< Stable identifier of the resolved link, independent of
    ///< the slug. May be empty - not checked by `Validate`.
    std::string link_id;

    ///< Host header of the inbound request, already sanitized/truncated by
    ///< `ClickEventSanitizer::SanitizeDomain`; must be non-empty per `Validate`.
    std::string domain;
    ///< HTTP status code the redirect responded with; must be a 3xx
    ///< redirect, or exactly 404 or 410, per `Validate`.
    uint16_t status_code = 0;

    ///< Sanitized `Referer` header value, truncated to
    ///< `SanitizationLimits::max_referrer_bytes`. May be
    ///< empty - not checked by `Validate`.
    std::string referrer;

    ///< Sanitized `User-Agent` header value, truncated
    ///< to `SanitizationLimits::max_user_agent_bytes`.
    ///< May be empty - not checked by `Validate`.
    std::string user_agent;

    ///< HMAC-SHA256 hash of the client identifier (see `ClientIdHasher`); 
    ///< the raw identifier is never stored. May be empty when the identifier was missing
    ///< and `ClientIdHashConfig::allow_missing_identifier` was set - not checked by `Validate`.
    std::string client_id_hash;

    /**
     * @brief Checks that this event's required fields are populated and
     * within the limits `ClickEventBuilder` is expected to have already
     * enforced.
     * @param[out] error If non-null and validation fails, receives
     * `AnalyticsErrorCode::validation` with message "Invalid click event";
     * left untouched on success.
     * @return `true` if `event_id`, `slug`, and `domain` are non-empty,
     * `occurred_at` is not the default-constructed `Timestamp{}`, and
     * `status_code` is a 3xx value or exactly 404/410; `false` otherwise.
     * Note: `link_id`, `referrer`, `user_agent`, and `client_id_hash` are
     * not checked - they may legitimately be empty.
     */
    bool Validate(AnalyticsError* error = nullptr) const;
};

}  // namespace url_shortener::analytics
