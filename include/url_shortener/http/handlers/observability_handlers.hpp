#pragma once

#include <url_shortener/http/handler_types.hpp>

namespace url_shortener
{
namespace http
{

/// @brief Handles `GET /healthz`; liveness probe.
/// @param req     Incoming request.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; not used.
/// @return 200 `text/plain` with body `"ok\n"` unconditionally.
BeastResponse handleHealthz(const BeastRequest& req,
                            const ::ServerConfig& config,
                            bool is_tls,
                            const RouteContext& context);

/// @brief Handles `GET /readyz`; readiness probe.
///
/// Currently identical to `handleHealthz`: responds 200 without
/// checking storage or other dependency health.
/// @param req     Incoming request.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; not used.
/// @return 200 `text/plain` with body `"ok\n"` unconditionally.
BeastResponse handleReadyz(const BeastRequest& req,
                           const ::ServerConfig& config,
                           bool is_tls,
                           const RouteContext& context);

/// @brief Handles `GET /metrics`; serves the in-process
/// Prometheus-style metrics.
/// @param req     Incoming request.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; not used.
/// @return 200 `text/plain` with the output of `renderMetrics()`.
BeastResponse handleMetrics(const BeastRequest& req,
                            const ::ServerConfig& config,
                            bool is_tls,
                            const RouteContext& context);

}  // namespace http
}  // namespace url_shortener
