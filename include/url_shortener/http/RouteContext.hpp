#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace url_shortener {
namespace http {

/// Named path parameters captured from a matched route pattern.
using PathParams = std::unordered_map<std::string, std::string>;

/// Per-request routing context passed from the router to a route handler.
struct RouteContext
{
    PathParams path_params;  ///< Captured URI template parameters.
    std::string query_string;  ///< Raw query string without the leading '?'.
    std::string route_label;  ///< Stable metrics/logging route label.
};

/// Returns a captured path parameter value when present.
inline std::optional<std::string> pathParam(
    const RouteContext& context,
    const std::string& name)
{
    const auto it = context.path_params.find(name);
    if (it == context.path_params.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace http
} // namespace url_shortener
