#pragma once

#include <url_shortener/http/handler_types.hpp>

namespace url_shortener {
namespace http {

/// Handles generic URI-store fallback requests.
BeastResponse handleFallbackUriStore(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

} // namespace http
} // namespace url_shortener
