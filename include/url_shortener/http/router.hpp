#pragma once

#include <optional>
#include <string>
#include <vector>

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http.hpp>
#include <url_shortener/http/handler_types.hpp>

namespace url_shortener
{
namespace http
{

/// @brief Read-only projection of a registered route for introspection
/// without exposing the handler callable.
struct RouteEntryView
{
    ///< HTTP method string, e.g. `GET` or `POST`.
    std::string method;

    ///< URI template registered with the router.
    std::string path_pattern;

    ///< Stable metrics/logging route label.
    std::string route_label;
};

/// @brief HTTP router that dispatches Beast requests by highest-path-
/// specificity, then first-registered-method order.
///
/// Dispatch selects the route whose path pattern has the greatest number
/// of literal (non-capture) segments among all patterns that match the
/// request target.  When two registered routes share the same specificity
/// for the same path, the first-registered route whose HTTP method matches
/// wins.  If the path matches but no registered method matches, the router
/// calls the error handler with status 405 (`"invalid_method"`) and sets an
/// `Allow` header listing the methods that path supports (RFC 9110 15.5.6);
/// HEAD is never listed because no HEAD route is served.  If no pattern
/// matches at all, it calls the error handler with 404 (`"not_found"`).
///
/// Thread-safety: `add()` is not thread-safe after `dispatch()` has been
/// called.  Build the router completely in a single thread before sharing
/// it across request-handling threads via a const reference.
class Router
{
public:
    /// @brief Constructs a router with the built-in JSON error formatter
    /// that emits `{"error":{"code":"...","message":"...","request_id":""}}`.
    Router();

    /// @brief Constructs a router with an application-supplied error
    /// formatter, allowing the routing layer to stay format-agnostic.
    /// @param error_handler Callable invoked on method-mismatch (400) or
    ///        route-not-found (404) instead of the built-in formatter.
    explicit Router(ErrorHandlerFn error_handler);

    /// @brief Appends a route to the registration list in call order.
    ///
    /// Registration order matters only as a tiebreaker when two routes
    /// share the same path specificity for the same path and method.
    /// @param method       HTTP verb this route responds to.
    /// @param path_pattern URI template; `{name}` segments capture
    ///        single non-empty path segments; a lone `{path}` segment
    ///        acts as a catch-all for any multi-segment path.
    /// @param route_label  Stable label written into the `RouteContext`
    ///        and used for metrics/logging.
    /// @param handler      Callable invoked when this route is matched.
    void add(boost::beast::http::verb method,
             const std::string& path_pattern,
             const std::string& route_label,
             HandlerFn handler);

    /// @brief Dispatches `req` to the highest-specificity matching route.
    /// @param req    Incoming request; `target()` is used for path matching.
    /// @param config Forwarded unchanged to the matched handler.
    /// @param is_tls Forwarded unchanged to the matched handler.
    /// @param peer   TCP peer (client) address of the connection, copied
    ///        onto the dispatched `RouteContext::peer`. Defaulted to
    ///        `std::nullopt` so existing call sites (tests, the legacy
    ///        session path) are behavior-neutral: a defaulted/absent peer
    ///        means "no peer" to every gate that reads it (fail-closed
    ///        locality, no throttle key). Never logged (client PII,
    ///        threat model T-70). Ignored by the redirect handlers
    ///        (contract C10).
    /// @return The response produced by the matched handler; a 405
    ///         Method Not Allowed error response (with an `Allow` header
    ///         listing the path's supported methods) if the path matched
    ///         but the HTTP method did not; a 404 error response if no
    ///         path pattern matched.
    BeastResponse dispatch(
        const BeastRequest& req,
        const ::ServerConfig& config,
        bool is_tls,
        std::optional<boost::asio::ip::address> peer = std::nullopt) const;

    /// @brief Returns a snapshot of registered route metadata.
    /// @return A copy of all route entries (method, pattern, label),
    ///         in registration order, without handler callables.
    std::vector<RouteEntryView> routes() const;

    /// @brief Matches a URI template against a request target and
    /// populates `context` with captured parameters and the query string.
    /// @param path_pattern URI template to match against.
    /// @param target       Raw request target, possibly including a `?`
    ///        query string.
    /// @param context      Receives captured path parameters, the raw
    ///        query string (without the leading `?`), and an empty
    ///        `route_label` (set by `dispatch()` after matching). Must
    ///        not be null; returns `false` immediately if null.
    /// @return `true` if `path_pattern` matches `target`; `false` if the
    ///         segment counts differ (special-cased for `{path}` catch-
    ///         all), any literal segment mismatches, any capture segment
    ///         is empty, or `context` is null.
    static bool matchPath(const std::string& path_pattern,
                          const std::string& target,
                          RouteContext* context);

private:
    /// @brief Full route entry held in the registration list; extends
    /// `RouteEntryView` with the handler callable.
    struct RouteEntry
    {
        ///< HTTP verb for this route.
        boost::beast::http::verb method;

        ///< URI template.
        std::string path_pattern;

        ///< Metrics/logging label.
        std::string route_label;

        ///< Dispatch target.
        HandlerFn handler;
    };

    ///< Registered routes in insertion order; order breaks ties among
    ///< routes of equal specificity matching the same path.
    std::vector<RouteEntry> routes_;

    ///< Error-response factory; set at construction, never modified.
    ErrorHandlerFn error_handler_;
};

}  // namespace http
}  // namespace url_shortener
