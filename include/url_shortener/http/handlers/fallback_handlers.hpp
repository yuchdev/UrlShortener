#pragma once

#include <url_shortener/http/handler_types.hpp>

namespace url_shortener
{
namespace http
{

/// @brief Handles generic URI-store fallback requests for paths not
/// matched by any more-specific route.
///
/// Rejects requests whose path starts with `/api/` or `/r/` with a 404
/// response to prevent reserved-prefix paths from being written to the
/// URI map.  All other GET/POST/DELETE requests delegate to
/// `handleApplicationRequest` (the legacy URI-store).
/// @param req     Incoming request; method and path determine behavior.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; not used.
/// @return 404 for paths beginning with `/api/` or `/r/`; otherwise the
///         response from `handleApplicationRequest`.
BeastResponse handleFallbackUriStore(const BeastRequest& req,
                                     const ::ServerConfig& config,
                                     bool is_tls,
                                     const RouteContext& context);

}  // namespace http
}  // namespace url_shortener
