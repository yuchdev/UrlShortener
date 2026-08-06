#pragma once

#include <url_shortener/http/HandlerTypes.hpp>

#include <boost/beast/http.hpp>

#include <string>
#include <vector>

namespace url_shortener {
namespace http {

/// Read-only route entry projection for tests and documentation tooling.
struct RouteEntryView
{
    std::string method;  ///< HTTP method string, e.g. GET or POST.
    std::string path_pattern;  ///< URI template registered with the router.
    std::string route_label;  ///< Stable metrics/logging route label.
};

/// Small first-match-wins router for Beast requests.
class Router
{
public:
    /// Constructs a router with a generic JSON error response formatter.
    Router();

    /// Constructs a router with an application-provided error formatter.
    explicit Router(ErrorHandlerFn error_handler);

    /// Registers a method-specific route in priority order.
    void add(
        boost::beast::http::verb method,
        const std::string& path_pattern,
        const std::string& route_label,
        HandlerFn handler);

    /// Dispatches a request to the first matching route handler.
    BeastResponse dispatch(
        const BeastRequest& req,
        const ::ServerConfig& config,
        bool is_tls) const;

    /// Returns route metadata without exposing handler callables.
    std::vector<RouteEntryView> routes() const;

    /// Matches a URI template against a request target and captures params.
    static bool matchPath(
        const std::string& path_pattern,
        const std::string& target,
        RouteContext* context);

private:
    struct RouteEntry
    {
        boost::beast::http::verb method;
        std::string path_pattern;
        std::string route_label;
        HandlerFn handler;
    };

    std::vector<RouteEntry> routes_;
    ErrorHandlerFn error_handler_;
};

} // namespace http
} // namespace url_shortener
