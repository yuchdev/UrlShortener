#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace url_shortener
{
namespace http
{

/// Named path parameters captured from a matched route pattern.
using PathParams = std::unordered_map<std::string, std::string>;

/// Per-request routing context passed from the router to a route handler.
struct RouteContext
{
    ///< Captured URI template parameters.
    PathParams path_params;

    ///< Raw query string without the leading '?'.
    std::string query_string;

    ///< Stable metrics/logging route label.
    std::string route_label;
};

/// @brief Returns a captured path parameter value when present.
/// @param context Routing context populated by `Router::matchPath`.
/// @param name    Name of the URI template capture (e.g. `"slug"`).
/// @return The captured string value if `name` is present in
///         `context.path_params`; `std::nullopt` otherwise.
inline std::optional<std::string> pathParam(const RouteContext& context,
                                            const std::string& name)
{
    const auto it = context.path_params.find(name);
    if (it == context.path_params.end()) {
        return std::nullopt;
    }
    return it->second;
}

}  // namespace http
}  // namespace url_shortener
