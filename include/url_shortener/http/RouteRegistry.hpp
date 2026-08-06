#pragma once

#include <string>
#include <vector>

namespace url_shortener {
namespace http {

struct RouteParameterDoc
{
    std::string name;
    std::string description;
    bool required = true;
};

struct RouteResponseDoc
{
    unsigned status = 0;
    std::string description;
};

/**
 * @brief Static description of a single HTTP route exposed by the server.
 *
 * Route descriptors provide an explicit, in-code registry that can be
 * enumerated for tooling, documentation, and metric-label lookups. This slice
 * intentionally does NOT drive request dispatch; dispatch remains owned by
 * @ref url_shortener::handleShortenerRequest. Descriptors are pure data with no
 * I/O or Beast coupling.
 */
struct RouteDescriptor
{
    /// HTTP method the route responds to (e.g. GET, POST, PATCH, DELETE, ANY).
    std::string method;
    /// URI pattern with `{param}` placeholders (e.g. /api/v1/links/{slug}/stats).
    std::string path_pattern;
    /// Metric route label matching @ref routeLabelForTarget output for the path.
    std::string route_label;
    /// Human-readable summary for documentation and future OpenAPI generation.
    std::string summary;
    /// Documentation grouping tags.
    std::vector<std::string> tags;
    /// Stable operation identifier for generated API references.
    std::string operation_id;
    /// Path parameter documentation matching placeholders in @ref path_pattern.
    std::vector<RouteParameterDoc> path_parameters;
    /// Query parameter documentation for documented query strings.
    std::vector<RouteParameterDoc> query_parameters;
    /// Request body documentation for routes that consume a body.
    std::string request_body_description;
    /// Response documentation for generated API references.
    std::vector<RouteResponseDoc> responses;
    /// True when this route is a compatibility alias for a canonical endpoint.
    bool compatibility_alias = false;
    /// True when this route intentionally returns a not-yet-implemented response.
    bool placeholder = false;
};

/**
 * @brief Returns the immutable registry of all known HTTP routes.
 *
 * The returned reference is to a function-local static populated once on first
 * use; it is read-only and safe to share across threads.
 *
 * @return Stable list of route descriptors covering the current endpoint matrix.
 */
const std::vector<RouteDescriptor>& registeredRoutes();

/**
 * @brief Classifies a request target into a stable metric route label.
 *
 * Preserves the exact behavior of the legacy @c routeLabelFor: it matches the
 * fixed observability endpoints, the redirect prefix, the link/API prefixes,
 * root redirects, and finally the application fallback.
 *
 * @param target Raw request target (path plus optional query string).
 * @return One of: metrics, healthz, readyz, redirect_prefixed, api_links,
 *         redirect_root, app.
 */
std::string routeLabelForTarget(const std::string& target);

} // namespace http
} // namespace url_shortener
