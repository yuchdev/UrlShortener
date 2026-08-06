#pragma once

#include <url_shortener/core/config.h>
#include <url_shortener/http/RouteContext.hpp>

#include <boost/beast/http.hpp>

#include <functional>
#include <string>

namespace url_shortener {
namespace http {

/// Beast request type used by HTTP route handlers.
using BeastRequest =
    boost::beast::http::request<boost::beast::http::string_body>;

/// Beast response type returned by HTTP route handlers.
using BeastResponse =
    boost::beast::http::response<boost::beast::http::string_body>;

/// Synchronous route handler callback used by the in-process router.
using HandlerFn = std::function<BeastResponse(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context)>;

/// Error response callback used to keep routing infrastructure format-agnostic.
using ErrorHandlerFn = std::function<BeastResponse(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    unsigned status,
    const std::string& code,
    const std::string& message)>;

} // namespace http
} // namespace url_shortener
