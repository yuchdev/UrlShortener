#pragma once

#include <url_shortener/http/handler_types.hpp>

namespace url_shortener
{
namespace http
{

/// @brief Handles `POST /api/v1/short-urls`; compatibility alias for
/// `POST /api/v1/links` that additionally accepts `code` as an
/// alternative to `slug` in the JSON body.
/// @param req     Incoming request; body may contain either `slug` or
///        the legacy `code` field as the caller-supplied short code.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; not used.
/// @return Same response semantics as
///         `createLinkFromRequest(slug_or_code)`.
BeastResponse handleCompatCreateLink(const BeastRequest& req,
                                     const ::ServerConfig& config,
                                     bool is_tls,
                                     const RouteContext& context);

/// @brief Handles `GET /api/v1/short-urls/{slug}`; compatibility alias
/// for `GET /api/v1/links/{slug}`.
/// @param req     Incoming request.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; `slug` path parameter identifies the
///        link.
/// @return Same as `handleGetLinkBySlug`.
BeastResponse handleCompatGetLinkBySlug(const BeastRequest& req,
                                        const ::ServerConfig& config,
                                        bool is_tls,
                                        const RouteContext& context);

}  // namespace http
}  // namespace url_shortener
