#pragma once

#include <url_shortener/http/Router.hpp>

namespace url_shortener {
namespace http {

/// Builds the application router with routes registered in priority order.
class RouterBuilder
{
public:
    /// Returns a router containing the current REST endpoint matrix.
    static Router buildApplicationRouter();
};

} // namespace http
} // namespace url_shortener
