#pragma once

#include <url_shortener/http/HandlerTypes.hpp>

namespace url_shortener {
namespace http {

/// Handles `POST /api/v1/short-urls` with legacy `code` support.
BeastResponse handleCompatCreateLink(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

/// Handles `GET /api/v1/short-urls/{slug}`.
BeastResponse handleCompatGetLinkBySlug(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

} // namespace http
} // namespace url_shortener
