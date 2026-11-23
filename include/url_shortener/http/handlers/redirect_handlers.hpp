#pragma once

#include <url_shortener/http/handler_types.hpp>

namespace url_shortener
{
namespace http
{

/// @brief Handles `GET /r/{slug}`; redirect fast path for the prefixed
/// route.
///
/// This is a performance-sensitive path: validates the slug, looks up
/// the link (via the cache-aside repository), checks lifecycle status,
/// enforces policy hooks (password guard, rate-limit guard), increments
/// redirect stats, writes an analytics event to both the in-process
/// `analyticsClickRepository()` singleton and the analytics event queue,
/// and issues a 301/302 redirect.  Analytics events are written for every
/// outcome, including 404/410/429 error responses.
/// @param req     Incoming request; `Referer`, `User-Agent`, and `Host`
///        headers are used for analytics event construction.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; `slug` path parameter identifies the
///        link to redirect to.
/// @return 301 or 302 on a successful redirect (HTTP status determined
///         by the link's `redirect_type`); 404 if the slug is invalid or
///         not found; 410 if the link is disabled or expired; 429 if a
///         policy hook blocks the request.
BeastResponse handlePrefixedRedirect(const BeastRequest& req,
                                     const ::ServerConfig& config,
                                     bool is_tls,
                                     const RouteContext& context);

/// @brief Handles `GET /{slug}`; redirect fast path for root-level slugs
/// with URI-store fallback.
///
/// Requests that are not slug-shaped, have a query string, or use a non-
/// GET method are delegated to `handleApplicationRequest` (URI-store
/// fallback) without writing analytics.  For valid slug-shaped paths,
/// missing or inactive links return redirect API error responses rather
/// than the URI-store content.  Analytics events for all redirect outcomes
/// are written to both the in-process `analyticsClickRepository()`
/// singleton and the analytics event queue.
/// @param req     Incoming request; affects slug extraction and fallback
///        decision.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; `slug` path parameter.
/// @return 301 or 302 on a successful redirect; 404 if the link is not
///         found; 410 if the link is disabled or expired; 429 if a policy
///         hook blocks the request; falls through to the URI-store
///         response for non-slug-shaped or query-bearing paths.
BeastResponse handleRootRedirect(const BeastRequest& req,
                                 const ::ServerConfig& config,
                                 bool is_tls,
                                 const RouteContext& context);

}  // namespace http
}  // namespace url_shortener
