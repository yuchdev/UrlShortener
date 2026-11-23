#pragma once

#include <string>

#include "url_shortener/analytics/analytics_service.hpp"

namespace url_shortener::http
{

/**
 * @brief Input bundle collected by a redirect handler before invoking
 * `RedirectAnalyticsHook::OnRedirectOutcome`.
 *
 * All string fields are raw values extracted directly from the request;
 * the hook is responsible for normalizing and sanitizing them (e.g.
 * stripping the port from `host`) before forwarding to
 * `AnalyticsService`.
 */
struct RedirectAnalyticsInput
{
    ///< Short-link slug that was resolved (without the `/r/` prefix).
    std::string slug;

    ///< Stable identifier of the resolved link; empty when the link was
    ///< not found (e.g. for a 404 event).
    std::string link_id;

    ///< Raw `Host` header value from the inbound request; may include a
    ///< port suffix that the hook strips before recording.
    std::string host;

    ///< Raw `Referer` header value; may be empty.
    std::string referrer;

    ///< Raw `User-Agent` header value; may be empty.
    std::string user_agent;

    ///< Client network identifier (typically `X-Forwarded-For` or the
    ///< remote address); hashed by `AnalyticsService` before storage.
    std::string client_network_identifier;

    ///< HTTP response status code the redirect handler returned
    ///< (e.g. 301, 302, 404, 410).
    std::uint16_t status_code = 0;
};

/**
 * @brief HTTP-layer bridge that translates a completed redirect outcome
 * into a normalized analytics event and forwards it to
 * `AnalyticsService::RecordRedirectAttempt`.
 *
 * Called from redirect handlers on every redirect attempt, including
 * failures (404, 410, 429). The call is `noexcept`; analytics failures
 * never surface to the redirect response.
 *
 * Lifetime: does not own `service`; the caller (composition root) must
 * keep it alive for at least as long as this object.
 */
class RedirectAnalyticsHook
{
public:
    /**
     * @brief Stores a reference to `service`; does not start any
     * background work.
     * @param service Analytics service that receives normalized redirect
     *        events; must outlive this object.
     */
    explicit RedirectAnalyticsHook(analytics::AnalyticsService& service)
        : service_(service)
    {
    }

    /**
     * @brief Translates `input` into a `RedirectEventContext`, strips
     * the port suffix from `input.host`, stamps the current wall clock
     * time as `occurred_at`, and calls
     * `service_.RecordRedirectAttempt`.
     * @param input Redirect outcome data collected by the calling handler.
     * @note `noexcept`: analytics failures are silently swallowed and
     *       must never affect the redirect response or latency.
     */
    void OnRedirectOutcome(const RedirectAnalyticsInput& input) noexcept;

private:
    ///< Analytics service receiving the normalized event; not owned,
    ///< must outlive this object.
    analytics::AnalyticsService& service_;
};

}  // namespace url_shortener::http
