#include <url_shortener/http/Router.hpp>

#include <algorithm>
#include <sstream>

namespace url_shortener {
namespace http {

namespace {

namespace bhttp = boost::beast::http;

std::string methodToString(const bhttp::verb method)
{
    switch (method) {
    case bhttp::verb::get: return "GET";
    case bhttp::verb::post: return "POST";
    case bhttp::verb::patch: return "PATCH";
    case bhttp::verb::delete_: return "DELETE";
    default: return std::string(bhttp::to_string(method));
    }
}

std::vector<std::string> splitPath(const std::string& path)
{
    std::vector<std::string> segments;
    std::size_t start = 0;
    while (start < path.size() && path[start] == '/') {
        ++start;
    }
    while (start <= path.size()) {
        const auto end = path.find('/', start);
        const auto count = end == std::string::npos ? std::string::npos : end - start;
        segments.push_back(path.substr(start, count));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    if (segments.size() == 1 && segments.front().empty()) {
        segments.clear();
    }
    return segments;
}

bool isCaptureSegment(const std::string& segment)
{
    if (segment.size() < 3 || segment.front() != '{' || segment.back() != '}') {
        return false;
    }
    const auto first = segment[1];
    if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_')) {
        return false;
    }
    return std::all_of(segment.begin() + 2, segment.end() - 1, [](const char c) {
        return (c >= 'A' && c <= 'Z')
               || (c >= 'a' && c <= 'z')
               || (c >= '0' && c <= '9')
               || c == '_';
    });
}

std::string captureName(const std::string& segment)
{
    return segment.substr(1, segment.size() - 2);
}

std::size_t pathSpecificity(const std::string& path_pattern)
{
    const auto segments = splitPath(path_pattern);
    return static_cast<std::size_t>(std::count_if(
        segments.begin(),
        segments.end(),
        [](const std::string& segment) {
            return !isCaptureSegment(segment);
        }));
}

BeastResponse defaultErrorHandler(
    const BeastRequest& req,
    const ::ServerConfig&,
    const bool,
    const unsigned status,
    const std::string& code,
    const std::string& message)
{
    BeastResponse res{static_cast<bhttp::status>(status), req.version()};
    res.set(bhttp::field::content_type, "application/json");
    std::ostringstream body;
    body << "{\"error\":{\"code\":\"" << code
         << "\",\"message\":\"" << message
         << "\",\"request_id\":\"\"}}";
    res.body() = body.str();
    res.prepare_payload();
    return res;
}

} // namespace

Router::Router()
    : error_handler_(defaultErrorHandler)
{
}

Router::Router(ErrorHandlerFn error_handler)
    : error_handler_(std::move(error_handler))
{
}

void Router::add(
    const bhttp::verb method,
    const std::string& path_pattern,
    const std::string& route_label,
    HandlerFn handler)
{
    routes_.push_back(RouteEntry{method, path_pattern, route_label, std::move(handler)});
}

BeastResponse Router::dispatch(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls) const
{
    bool path_matched = false;
    std::size_t best_specificity = 0;
    for (const auto& route : routes_) {
        RouteContext context;
        if (!matchPath(route.path_pattern, std::string(req.target()), &context)) {
            continue;
        }
        const auto specificity = pathSpecificity(route.path_pattern);
        if (!path_matched || specificity > best_specificity) {
            best_specificity = specificity;
            path_matched = true;
        }
    }

    bhttp::verb only_allowed_method = bhttp::verb::unknown;
    bool multiple_allowed_methods = false;
    for (const auto& route : routes_) {
        RouteContext context;
        if (!matchPath(route.path_pattern, std::string(req.target()), &context)) {
            continue;
        }
        if (pathSpecificity(route.path_pattern) != best_specificity) {
            continue;
        }
        if (only_allowed_method == bhttp::verb::unknown) {
            only_allowed_method = route.method;
        }
        else if (only_allowed_method != route.method) {
            multiple_allowed_methods = true;
        }
        if (route.method != req.method()) {
            continue;
        }
        context.route_label = route.route_label;
        return route.handler(req, config, is_tls, context);
    }

    if (path_matched) {
        const std::string message = multiple_allowed_methods
            ? "Unsupported operation"
            : "Only " + methodToString(only_allowed_method) + " is supported";
        return error_handler_(
            req,
            config,
            is_tls,
            400,
            "invalid_method",
            message);
    }

    return error_handler_(
        req,
        config,
        is_tls,
        404,
        "not_found",
        "Route not found");
}

std::vector<RouteEntryView> Router::routes() const
{
    std::vector<RouteEntryView> result;
    result.reserve(routes_.size());
    for (const auto& route : routes_) {
        result.push_back(RouteEntryView{
            methodToString(route.method),
            route.path_pattern,
            route.route_label});
    }
    return result;
}

bool Router::matchPath(
    const std::string& path_pattern,
    const std::string& target,
    RouteContext* context)
{
    if (context == nullptr) {
        return false;
    }
    context->path_params.clear();
    context->query_string.clear();
    context->route_label.clear();

    const auto query_pos = target.find('?');
    const auto path = query_pos == std::string::npos ? target : target.substr(0, query_pos);
    if (query_pos != std::string::npos) {
        context->query_string = target.substr(query_pos + 1);
    }

    const auto pattern_segments = splitPath(path_pattern);
    const auto path_segments = splitPath(path);
    if (pattern_segments.size() != path_segments.size()) {
        if (pattern_segments.size() == 1
            && isCaptureSegment(pattern_segments.front())
            && captureName(pattern_segments.front()) == "path"
            && !path.empty()
            && path != "/") {
            context->path_params["path"] = path.front() == '/'
                ? path.substr(1)
                : path;
            return true;
        }
        return false;
    }

    for (std::size_t i = 0; i < pattern_segments.size(); ++i) {
        const auto& pattern = pattern_segments[i];
        const auto& actual = path_segments[i];
        if (isCaptureSegment(pattern)) {
            if (actual.empty()) {
                context->path_params.clear();
                context->query_string.clear();
                return false;
            }
            context->path_params[captureName(pattern)] = actual;
            continue;
        }
        if (pattern != actual) {
            context->path_params.clear();
            context->query_string.clear();
            return false;
        }
    }

    return true;
}

} // namespace http
} // namespace url_shortener
