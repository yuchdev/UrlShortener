#pragma once

#include <url_shortener/http/handler_types.hpp>

namespace url_shortener {
namespace http {

/// Handles `GET /r/{slug}`.
BeastResponse handlePrefixedRedirect(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

/// Handles `GET /{slug}` with explicit fallback to the URI store.
BeastResponse handleRootRedirect(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

} // namespace http
} // namespace url_shortener
