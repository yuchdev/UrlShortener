#include <url_shortener/http/RouteRegistry.hpp>

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace url_shortener {
namespace http {

namespace {

// These prefixes mirror the constants used by the request dispatcher in
// request_handlers.cpp. They are duplicated here (rather than shared) to keep
// the registry free of dispatch/business coupling in this slice.
constexpr char shortener_api_prefix[] = "/api/v1/links";
constexpr char shortener_api_compat_prefix[] = "/api/v1/short-urls";
constexpr char short_redirect_prefix[] = "/r/";

bool startsWith(const std::string& value, const std::string& prefix)
{
    return value.rfind(prefix, 0) == 0;
}

std::string tagFor(const std::string& label, const std::string& path)
{
    if (label == "healthz" || label == "readyz" || label == "metrics") {
        return "Observability";
    }
    if (startsWith(path, shortener_api_compat_prefix)) {
        return "Compatibility";
    }
    if (startsWith(path, shortener_api_prefix)) {
        return "Links";
    }
    if (label == "redirect_prefixed" || path == "/{slug}") {
        return "Redirects";
    }
    return "Fallback";
}

std::string operationIdFor(const std::string& method, const std::string& path)
{
    std::string id;
    id.reserve(method.size() + path.size());
    for (const auto c : method) {
        id.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    for (const auto c : path) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            id.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        else if (!id.empty() && id.back() != '_') {
            id.push_back('_');
        }
    }
    while (!id.empty() && id.back() == '_') {
        id.pop_back();
    }
    return id;
}

std::vector<RouteParameterDoc> pathParametersFor(const std::string& path)
{
    std::vector<RouteParameterDoc> params;
    std::size_t pos = 0;
    while ((pos = path.find('{', pos)) != std::string::npos) {
        const auto end = path.find('}', pos + 1);
        if (end == std::string::npos) {
            break;
        }
        const auto name = path.substr(pos + 1, end - pos - 1);
        params.push_back(RouteParameterDoc{
            name,
            "Path parameter `" + name + "`.",
            true});
        pos = end + 1;
    }
    return params;
}

std::vector<RouteParameterDoc> queryParametersFor(const std::string& path)
{
    if (path == "/api/v1/links/{slug}/stats") {
        return {
            {"from", "Inclusive RFC3339 start timestamp for aggregate stats.", false},
            {"to", "Exclusive RFC3339 end timestamp for aggregate stats.", false},
            {"bucket", "Aggregate bucket size: hour, day, or week.", false},
        };
    }
    return {};
}

std::string requestBodyFor(const std::string& method, const std::string& path)
{
    if (method == "POST" && path == "/api/v1/links") {
        return "JSON object containing target_url and optional slug, metadata, "
               "tags, campaign, expiry, and redirect type fields.";
    }
    if (method == "POST" && path == "/api/v1/short-urls") {
        return "Compatibility JSON object containing url and optional code fields.";
    }
    if (method == "PATCH" && path == "/api/v1/links/{slug}") {
        return "JSON object containing mutable link fields to update; null clears "
               "nullable fields where supported.";
    }
    if (method == "POST" && path == "/{path}") {
        return "Raw URI-store value to associate with the requested path.";
    }
    if (method == "POST" && path == "/") {
        return "Raw URI-store value to associate with the root path.";
    }
    return {};
}

std::vector<RouteResponseDoc> responsesFor(
    const std::string& method,
    const std::string& path,
    const bool placeholder)
{
    if (placeholder) {
        return {{501, "Feature placeholder is not enabled."}};
    }
    if (method == "POST" && path == "/api/v1/links") {
        return {{201, "Link created."}, {400, "Invalid request."}};
    }
    if (method == "POST" && path == "/api/v1/short-urls") {
        return {{201, "Compatibility link created."}, {400, "Invalid request."}};
    }
    if (path == "/r/{slug}" || path == "/{slug}") {
        return {{301, "Permanent redirect."}, {302, "Temporary redirect."}, {404, "Link not found."}, {410, "Link inactive."}};
    }
    if (path == "/healthz" || path == "/readyz" || path == "/metrics") {
        return {{200, "Successful response."}};
    }
    if (path == "/{path}" || path == "/") {
        return {{200, "URI-store operation succeeded."}, {404, "URI not found."}};
    }
    if (method == "DELETE") {
        return {{200, "Resource deleted."}, {404, "Resource not found."}};
    }
    return {{200, "Successful response."}, {404, "Resource not found."}};
}

RouteDescriptor route(
    std::string method,
    std::string path,
    std::string label,
    std::string summary,
    const bool compatibility_alias = false,
    const bool placeholder = false)
{
    RouteDescriptor descriptor;
    descriptor.method = std::move(method);
    descriptor.path_pattern = std::move(path);
    descriptor.route_label = std::move(label);
    descriptor.summary = std::move(summary);
    descriptor.tags = {tagFor(descriptor.route_label, descriptor.path_pattern)};
    descriptor.operation_id = operationIdFor(descriptor.method, descriptor.path_pattern);
    descriptor.path_parameters = pathParametersFor(descriptor.path_pattern);
    descriptor.query_parameters = queryParametersFor(descriptor.path_pattern);
    descriptor.request_body_description = requestBodyFor(
        descriptor.method,
        descriptor.path_pattern);
    descriptor.responses = responsesFor(
        descriptor.method,
        descriptor.path_pattern,
        placeholder);
    descriptor.compatibility_alias = compatibility_alias;
    descriptor.placeholder = placeholder;
    return descriptor;
}

} // namespace

std::string routeLabelForTarget(const std::string& target)
{
    if (target == "/metrics") {
        return "metrics";
    }
    if (target == "/healthz") {
        return "healthz";
    }
    if (target == "/readyz") {
        return "readyz";
    }
    if (startsWith(target, short_redirect_prefix)) {
        return "redirect_prefixed";
    }
    if (startsWith(target, shortener_api_prefix)
        || startsWith(target, shortener_api_compat_prefix)) {
        return "api_links";
    }
    if (target.size() > 1 && target[0] == '/') {
        return "redirect_root";
    }
    return "app";
}

const std::vector<RouteDescriptor>& registeredRoutes()
{
    static const std::vector<RouteDescriptor> routes = {
        // Observability endpoints.
        route("GET", "/healthz", "healthz",
         "Liveness probe; returns 200 when the process is running."),
        route("GET", "/readyz", "readyz",
         "Readiness probe; returns 200 when the server can serve traffic."),
        route("GET", "/metrics", "metrics",
         "Prometheus-style metrics exposition endpoint."),

        // Link management API (/api/v1/links).
        route("POST", "/api/v1/links", "api_links",
         "Create a short link from a target URL and optional attributes."),
        route("GET", "/api/v1/links/id/{id}", "api_links",
         "Fetch a link by its opaque identifier."),
        route("GET", "/api/v1/links/{slug}", "api_links",
         "Fetch a link's full metadata by slug."),
        route("PATCH", "/api/v1/links/{slug}", "api_links",
         "Partially update a link's mutable attributes."),
        route("DELETE", "/api/v1/links/{slug}", "api_links",
         "Soft-delete a link by slug."),
        route("GET", "/api/v1/links/{slug}/preview", "api_links",
         "Preview a link's resolved status without redirecting."),
        route("GET", "/api/v1/links/{slug}/qr", "api_links",
         "QR code generation placeholder (not yet implemented).", false, true),
        route("GET", "/api/v1/links/{slug}/routing", "api_links",
         "Routing rules placeholder (not yet implemented).", false, true),
        route("GET", "/api/v1/links/{slug}/stats", "api_links",
         "Aggregate click statistics for a link."),
        route("POST", "/api/v1/links/{slug}/enable", "api_links",
         "Enable a previously disabled link."),
        route("POST", "/api/v1/links/{slug}/disable", "api_links",
         "Disable a link so it stops redirecting."),
        route("POST", "/api/v1/links/{slug}/restore", "api_links",
         "Restore a soft-deleted link."),

        // Compatibility API (/api/v1/short-urls).
        route("POST", "/api/v1/short-urls", "api_links",
         "Compatibility alias for creating a short link.", true),
        route("GET", "/api/v1/short-urls/{slug}", "api_links",
         "Compatibility alias for fetching a link by slug.", true),

        // Redirect endpoints.
        route("GET", "/r/{slug}", "redirect_prefixed",
         "Prefixed redirect to a link's target URL."),
        route("GET", "/{slug}", "redirect_root",
         "Root redirect to a link's target URL."),

        // Generic URI store fallback.
        route("GET", "/{path}", "redirect_root",
         "Fallback URI store read for arbitrary application paths."),
        route("POST", "/{path}", "redirect_root",
         "Fallback URI store write for arbitrary application paths."),
        route("DELETE", "/{path}", "redirect_root",
         "Fallback URI store delete for arbitrary application paths."),
        route("GET", "/", "app",
         "Fallback URI store read for the root application path."),
        route("POST", "/", "app",
         "Fallback URI store write for the root application path."),
        route("DELETE", "/", "app",
         "Fallback URI store delete for the root application path."),
    };
    return routes;
}

} // namespace http
} // namespace url_shortener
