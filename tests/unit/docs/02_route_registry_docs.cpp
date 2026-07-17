#define BOOST_TEST_MODULE RouteRegistryDocs
#include <boost/test/unit_test.hpp>

#include <url_shortener/http/RouteRegistry.hpp>

#include <sstream>
#include <string>

namespace route_http = url_shortener::http;

namespace {

std::string renderMarkdown()
{
    std::ostringstream out;
    out << "# URL Shortener API Reference\n\n"
        << "Generated from `url_shortener::http::registeredRoutes()`.\n\n"
        << "| Method | Path | Operation ID | Tags | Notes | Summary |\n"
        << "|---|---|---|---|---|---|\n";

    for (const auto& route : route_http::registeredRoutes()) {
        std::string notes;
        if (route.compatibility_alias) {
            notes += "compatibility alias";
        }
        if (route.placeholder) {
            if (!notes.empty()) {
                notes += "; ";
            }
            notes += "placeholder";
        }
        if (notes.empty()) {
            notes = "-";
        }
        out << "| " << route.method
            << " | `" << route.path_pattern
            << "` | `" << route.operation_id
            << "` | " << route.tags.front()
            << " | " << notes
            << " | " << route.summary
            << " |\n";
    }
    return out.str();
}

} // namespace

BOOST_AUTO_TEST_CASE(generated_reference_contains_all_routes)
{
    const auto markdown = renderMarkdown();
    for (const auto& route : route_http::registeredRoutes()) {
        const auto needle = "| " + route.method + " | `" + route.path_pattern + "` |";
        BOOST_TEST(markdown.find(needle) != std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(generated_reference_includes_parameters_and_markers)
{
    const auto markdown = renderMarkdown();
    BOOST_TEST(markdown.find("{slug}") != std::string::npos);
    BOOST_TEST(markdown.find("{path}") != std::string::npos);
    BOOST_TEST(markdown.find("compatibility alias") != std::string::npos);
    BOOST_TEST(markdown.find("placeholder") != std::string::npos);
}
