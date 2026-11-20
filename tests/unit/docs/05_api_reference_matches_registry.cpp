#define BOOST_TEST_MODULE DocsApiReferenceMatchesRegistry
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "doc_table.hpp"
#include "url_shortener/http/route_registry.hpp"

namespace route_http = url_shortener::http;

namespace
{

/// @brief Joins documentation grouping tags with `", "`.
///
/// Mirrors the identical helper in `tools/route_registry_dump.cpp`; the two
/// must render byte-for-byte identically for the committed `docs/api/README.md`
/// to match.
///
/// @param tags The route's tags, in registry order.
/// @return `"a, b"`-style joined list (empty string when @p tags is empty).
std::string join_tags(const std::vector<std::string>& tags)
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

/// @brief Renders the `Notes` cell (compatibility/placeholder markers).
///
/// Mirrors `markersFor` in `tools/route_registry_dump.cpp`.
///
/// @param route The route descriptor.
/// @return `"compatibility alias"`, `"placeholder"`, both joined by `"; "`, or
/// `"-"` when neither marker is set.
std::string markers_for(const route_http::RouteDescriptor& route)
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

/// @brief Renders the `Query params` cell.
///
/// Mirrors `joinParameters` in `tools/route_registry_dump.cpp`.
///
/// @param params The route's documented query parameters.
/// @return Backtick-wrapped names joined by `", "`, or `"-"` when empty.
std::string join_parameters(
    const std::vector<route_http::RouteParameterDoc>& params)
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

/// @brief Renders the full API-reference Markdown from the live registry.
///
/// This is a faithful, self-contained duplicate of the rendering in
/// `tools/route_registry_dump.cpp` (the generator that produces
/// `docs/api/README.md`); `02_route_registry_docs.cpp` already duplicates its
/// own renderer the same way. Any change to the generator must be mirrored here
/// (and vice versa), which this test enforces by comparing the output to the
/// committed file.
///
/// @return The complete Markdown document the generator would emit.
std::string render_markdown()
{
    std::ostringstream out;
    out << "# URL Shortener API Reference\n\n"
        << "Generated from `url_shortener::http::registeredRoutes()`. The C++ "
           "route\n"
        << "registry is the source of truth for HTTP API inventory, route "
           "labels, operation\n"
        << "IDs, compatibility markers, placeholders, and documentation "
           "grouping.\n\n"
        << "| Method | Path | Operation ID | Tags | Query params | Request "
           "body "
           "| Notes | Summary |\n"
        << "|---|---|---|---|---|---|---|---|\n";

    for (const auto& route : route_http::registeredRoutes()) {
        out << "| " << route.method << " | `" << route.path_pattern << "` | `"
            << route.operation_id << "` | " << join_tags(route.tags) << " | "
            << join_parameters(route.query_parameters) << " | "
            << (route.request_body_description.empty()
                    ? "-"
                    : route.request_body_description)
            << " | " << markers_for(route) << " | " << route.summary << " |\n";
    }
    return out.str();
}

}  // namespace

// Type: unit (generated-documentation drift guard).
// Scenario: the committed docs/api/README.md must be byte-for-byte what
//   tools/route_registry_dump renders from the live route registry (after CRLF
//   normalization), so adding or editing a route in RouteRegistry without
//   regenerating the reference fails here.
// Boundaries: only \r\n is normalized (a Windows autocrlf checkout); the file
//   must otherwise match exactly - no BOM, no trailing-whitespace tolerance;
//   the renderer here is a faithful duplicate of the generator's (same
//   discipline as 02_route_registry_docs.cpp). The route -> command projection
//   in section 5 of docs/commands/README.md is owned by docs__04, not here.
// On failure first check: did RouteRegistry change without regenerating the
//   doc? Regenerate with the tool named in the failure message; never hand-edit
//   the generated file.

BOOST_AUTO_TEST_CASE(api_reference_is_current)
{
#ifndef SOURCE_DIR
    BOOST_TEST_MESSAGE("SOURCE_DIR not defined; run via CTest. Skipping.");
#else
    const std::string source_dir = SOURCE_DIR;
    const auto raw = doc_table::read_file(source_dir + "/docs/api/README.md");
    BOOST_REQUIRE_MESSAGE(
        raw.has_value(),
        "could not read docs/api/README.md under " << source_dir);

    const std::string on_disk = doc_table::normalize_crlf(*raw);
    const std::string rendered = render_markdown();

    BOOST_TEST(on_disk == rendered,
               "docs/api/README.md is out of date with the route registry; "
               "regenerate: cmake-build/route_registry_dump > "
               "docs/api/README.md");
#endif
}
