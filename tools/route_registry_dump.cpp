#include <url_shortener/http/RouteRegistry.hpp>

#include <iostream>
#include <string>

namespace {

std::string joinTags(const std::vector<std::string>& tags)
{
    std::string result;
    for (std::size_t i = 0; i < tags.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += tags[i];
    }
    return result;
}

std::string markersFor(const url_shortener::http::RouteDescriptor& route)
{
    std::string markers;
    if (route.compatibility_alias) {
        markers += "compatibility alias";
    }
    if (route.placeholder) {
        if (!markers.empty()) {
            markers += "; ";
        }
        markers += "placeholder";
    }
    return markers.empty() ? "-" : markers;
}

std::string joinParameters(const std::vector<url_shortener::http::RouteParameterDoc>& params)
{
    if (params.empty()) {
        return "-";
    }
    std::string result;
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += "`" + params[i].name + "`";
    }
    return result;
}

} // namespace

int main()
{
    std::cout << "# URL Shortener API Reference\n\n"
              << "Generated from `url_shortener::http::registeredRoutes()`. The C++ route\n"
              << "registry is the source of truth for HTTP API inventory, route labels, operation\n"
              << "IDs, compatibility markers, placeholders, and documentation grouping.\n\n"
              << "| Method | Path | Operation ID | Tags | Query params | Request body | Notes | Summary |\n"
              << "|---|---|---|---|---|---|---|---|\n";

    for (const auto& route : url_shortener::http::registeredRoutes()) {
        std::cout << "| " << route.method
                  << " | `" << route.path_pattern
                  << "` | `" << route.operation_id
                  << "` | " << joinTags(route.tags)
                  << " | " << joinParameters(route.query_parameters)
                  << " | " << (route.request_body_description.empty()
                                   ? "-"
                                   : route.request_body_description)
                  << " | " << markersFor(route)
                  << " | " << route.summary
                  << " |\n";
    }
    return 0;
}
