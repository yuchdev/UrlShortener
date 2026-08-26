#pragma once

#include <url_shortener/http/handler_types.hpp>

namespace url_shortener {
namespace http {

/// Handles the liveness endpoint `GET /healthz`.
BeastResponse handleHealthz(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

/// Handles the readiness endpoint `GET /readyz`.
BeastResponse handleReadyz(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

/// Handles the plaintext in-process metrics endpoint `GET /metrics`.
BeastResponse handleMetrics(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

} // namespace http
} // namespace url_shortener
