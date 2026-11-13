#pragma once

#include <url_shortener/http/router.hpp>

namespace url_shortener
{
namespace http
{

/// @brief Factory that constructs the application `Router` with all routes
/// registered in priority order.
///
/// The registration order is: observability (`/healthz`, `/readyz`,
/// `/metrics`) → canonical link API (`/api/v1/links/...`) → compatibility
/// API (`/api/v1/short-urls/...`) → redirect fast path (`/r/{slug}`,
/// `/{slug}`) → URI-store fallback (`/{path}`, `/`).  Within each group,
/// more-specific patterns are registered before less-specific ones so the
/// router's highest-specificity dispatch produces the expected routing
/// without ambiguity.
class RouterBuilder
{
public:
    /// @brief Constructs and returns a fully-configured application router.
    ///
    /// Uses `makeApiErrorResponse` from `request_handlers.hpp` as the
    /// injected error formatter so error responses match the JSON envelope
    /// produced by the rest of the API.
    /// @return A `Router` populated with the full current endpoint matrix;
    ///         the returned router is ready to serve requests immediately.
    static Router buildApplicationRouter();
};

}  // namespace http
}  // namespace url_shortener
