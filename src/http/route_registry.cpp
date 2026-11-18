#include <url_shortener/http/route_registry.hpp>

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
constexpr char shortener_api_commands_prefix[] = "/api/v1/commands";
constexpr char shortener_api_jobs_prefix[] = "/api/v1/jobs";
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
    if (startsWith(path, shortener_api_commands_prefix)
        || startsWith(path, shortener_api_jobs_prefix)) {
        return "Commands";
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

std::vector<RouteParameterDoc> queryParametersFor(const std::string& method,
                                                  const std::string& path)
{
    if (path == "/api/v1/links/{slug}/stats") {
        return {
            {"from", "Inclusive RFC3339 start timestamp for aggregate stats.", false},
            {"to", "Exclusive RFC3339 end timestamp for aggregate stats.", false},
            {"bucket", "Aggregate bucket size: hour, day, or week.", false},
        };
    }
    // The job list filters belong to GET /api/v1/jobs only (the sibling job
    // routes take a path id or no parameter), mirroring the link.list paging
    // convention they share.
    if (method == "GET" && path == "/api/v1/jobs") {
        return {
            {"state", "Lifecycle filter: queued, running, succeeded, failed, "
                      "or cancelled (lists all states when omitted).", false},
            {"offset", "Zero-based index of the first job to return "
                       "(0..1000000, default 0).", false},
            {"limit", "Maximum number of jobs to return (1..500, default 50).",
             false},
        };
    }
    // The list filters belong to GET /api/v1/links only; POST /api/v1/links
    // (create) shares the path but takes no query parameters, so it must not
    // inherit these (keeps its generated row byte-identical, contract C3).
    if (method == "GET" && path == "/api/v1/links") {
        return {
            {"status", "Lifecycle filter: all, active, disabled, expired, or "
                       "deleted (default active). all and deleted require "
                       "write/admin.", false},
            {"q", "Case-insensitive substring filter on slug or target URL.", false},
            {"tag", "Exact-match tag filter.", false},
            {"campaign", "Exact-match campaign name filter.", false},
            {"sort", "Sort field: created_at (default), slug, or updated_at.", false},
            {"order", "Sort direction: desc (default) or asc.", false},
            {"offset", "Zero-based index of the first link to return.", false},
            {"limit", "Maximum number of links to return (1..500, default 50).", false},
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
    if (method == "POST" && path == "/api/v1/commands") {
        return "Command request envelope: an object with a command name, an "
               "args object, optional options (dry_run/async) and id.";
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
    if (method == "GET" && path == "/api/v1/links") {
        return {{200, "Links listed."}, {400, "Invalid request."}};
    }
    if (method == "POST" && path == "/api/v1/links") {
        return {{201, "Link created."}, {400, "Invalid request."}};
    }
    if (method == "POST" && path == "/api/v1/short-urls") {
        return {{201, "Compatibility link created."}, {400, "Invalid request."}};
    }
    if (method == "GET" && path == "/api/v1/commands") {
        return {{200, "Registered commands listed."},
                {400, "Command API disabled, or a malformed/unknown query "
                      "parameter."},
                {403, "Not authorized."}};
    }
    if (method == "POST" && path == "/api/v1/commands") {
        return {{200, "Command executed; response envelope returned."},
                {400, "Invalid request envelope."},
                {404, "Command API disabled or unknown command."},
                {415, "Content-Type must be application/json."}};
    }
    if (method == "GET" && path == "/api/v1/jobs") {
        return {{200, "Jobs listed."},
                {400, "Malformed or invalid query parameter."},
                {404, "Command API disabled."}};
    }
    if (method == "GET" && path == "/api/v1/jobs/{id}") {
        return {{200, "Job record returned."},
                {404, "Command API disabled or unknown job."}};
    }
    if (method == "POST" && path == "/api/v1/jobs/{id}/cancel") {
        return {{200, "Cancellation requested; job record returned."},
                {403, "Not authorized."},
                {404, "Command API disabled or unknown job."},
                {415, "Content-Type must be application/json."}};
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
    descriptor.query_parameters = queryParametersFor(
        descriptor.method,
        descriptor.path_pattern);
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
    if (startsWith(target, shortener_api_commands_prefix)
        || startsWith(target, shortener_api_jobs_prefix)) {
        return "api_commands";
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
        route("GET", "/api/v1/links", "api_links",
         "List links with filtering, sorting and paging."),
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

        // Generic command API (/api/v1/commands), gated by
        // --command-api-enabled.
        route("GET", "/api/v1/commands", "api_commands",
         "List the registered commands and their argument schemas (gated by "
         "--command-api-enabled)."),
        route("POST", "/api/v1/commands", "api_commands",
         "Execute one command request envelope and return its response "
         "envelope (gated by --command-api-enabled)."),

        // Job inspection API (/api/v1/jobs), gated by --command-api-enabled;
        // thin projections onto job.list / job.get / job.cancel.
        route("GET", "/api/v1/jobs", "api_commands",
         "List job records with state filter and paging (gated by "
         "--command-api-enabled)."),
        route("GET", "/api/v1/jobs/{id}", "api_commands",
         "Fetch one job record by id (gated by --command-api-enabled)."),
        route("POST", "/api/v1/jobs/{id}/cancel", "api_commands",
         "Request cancellation of a job by id (gated by "
         "--command-api-enabled)."),

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
