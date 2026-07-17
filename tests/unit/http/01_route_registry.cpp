#define BOOST_TEST_MODULE RouteRegistry
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <set>
#include <string>

#include "url_shortener/http/RouteRegistry.hpp"

using url_shortener::http::registeredRoutes;
using url_shortener::http::RouteDescriptor;
using url_shortener::http::routeLabelForTarget;

namespace {

bool hasRoute(const std::string& method, const std::string& path_pattern)
{
    const auto& routes = registeredRoutes();
    return std::any_of(routes.begin(), routes.end(),
                       [&](const RouteDescriptor& route) {
                           return route.method == method
                                  && route.path_pattern == path_pattern;
                       });
}

const RouteDescriptor* findRoute(const std::string& method, const std::string& path_pattern)
{
    const auto& routes = registeredRoutes();
    const auto it = std::find_if(routes.begin(), routes.end(),
                       [&](const RouteDescriptor& route) {
                           return route.method == method
                                  && route.path_pattern == path_pattern;
                       });
    return it == routes.end() ? nullptr : &*it;
}

std::set<std::string> placeholdersFor(const std::string& path_pattern)
{
    std::set<std::string> placeholders;
    std::size_t pos = 0;
    while ((pos = path_pattern.find('{', pos)) != std::string::npos) {
        const auto end = path_pattern.find('}', pos + 1);
        if (end == std::string::npos) {
            break;
        }
        placeholders.insert(path_pattern.substr(pos + 1, end - pos - 1));
        pos = end + 1;
    }
    return placeholders;
}

std::string sampleTargetFor(const std::string& path_pattern)
{
    std::string sample = path_pattern;
    const std::pair<std::string, std::string> replacements[] = {
        {"{id}", "id-123"},
        {"{slug}", "abc123"},
        {"{path}", "stored-resource"},
    };

    for (const auto& replacement : replacements) {
        const auto pos = sample.find(replacement.first);
        if (pos != std::string::npos) {
            sample.replace(pos, replacement.first.size(), replacement.second);
        }
    }
    return sample;
}

} // namespace

// Normal case: labels match the legacy routeLabelFor classification exactly.
BOOST_AUTO_TEST_CASE(route_label_preserves_existing_labels)
{
    BOOST_TEST(routeLabelForTarget("/metrics") == "metrics");
    BOOST_TEST(routeLabelForTarget("/healthz") == "healthz");
    BOOST_TEST(routeLabelForTarget("/readyz") == "readyz");
    BOOST_TEST(routeLabelForTarget("/r/abc123") == "redirect_prefixed");
    BOOST_TEST(routeLabelForTarget("/api/v1/links") == "api_links");
    BOOST_TEST(routeLabelForTarget("/api/v1/links/abc/stats") == "api_links");
    BOOST_TEST(routeLabelForTarget("/api/v1/short-urls/abc") == "api_links");
    BOOST_TEST(routeLabelForTarget("/abc123") == "redirect_root");
    BOOST_TEST(routeLabelForTarget("/") == "app");
}

// Edge case: empty target and non-slash target fall back to the app label.
BOOST_AUTO_TEST_CASE(route_label_edge_cases_fall_back_to_app)
{
    BOOST_TEST(routeLabelForTarget("") == "app");
    BOOST_TEST(routeLabelForTarget("healthz") == "app");
    // Prefix precedence: /api/v1/links wins over generic root redirect.
    BOOST_TEST(routeLabelForTarget("/api/v1/links/id/xyz") == "api_links");
    // The redirect prefix must win over the api/root classification.
    BOOST_TEST(routeLabelForTarget("/r/") == "redirect_prefixed");
}

// Enumeration must cover the key documented endpoints across all labels.
BOOST_AUTO_TEST_CASE(route_registry_contains_key_endpoints)
{
    BOOST_TEST(!registeredRoutes().empty());

    BOOST_TEST(hasRoute("GET", "/healthz"));
    BOOST_TEST(hasRoute("GET", "/readyz"));
    BOOST_TEST(hasRoute("GET", "/metrics"));
    BOOST_TEST(hasRoute("POST", "/api/v1/links"));
    BOOST_TEST(hasRoute("GET", "/api/v1/links/id/{id}"));
    BOOST_TEST(hasRoute("GET", "/api/v1/links/{slug}"));
    BOOST_TEST(hasRoute("PATCH", "/api/v1/links/{slug}"));
    BOOST_TEST(hasRoute("DELETE", "/api/v1/links/{slug}"));
    BOOST_TEST(hasRoute("GET", "/api/v1/links/{slug}/preview"));
    BOOST_TEST(hasRoute("GET", "/api/v1/links/{slug}/qr"));
    BOOST_TEST(hasRoute("GET", "/api/v1/links/{slug}/routing"));
    BOOST_TEST(hasRoute("GET", "/api/v1/links/{slug}/stats"));
    BOOST_TEST(hasRoute("POST", "/api/v1/links/{slug}/enable"));
    BOOST_TEST(hasRoute("POST", "/api/v1/links/{slug}/disable"));
    BOOST_TEST(hasRoute("POST", "/api/v1/links/{slug}/restore"));
    BOOST_TEST(hasRoute("POST", "/api/v1/short-urls"));
    BOOST_TEST(hasRoute("GET", "/api/v1/short-urls/{slug}"));
    BOOST_TEST(hasRoute("GET", "/r/{slug}"));
    BOOST_TEST(hasRoute("GET", "/{slug}"));
    BOOST_TEST(hasRoute("GET", "/{path}"));
    BOOST_TEST(hasRoute("POST", "/{path}"));
    BOOST_TEST(hasRoute("DELETE", "/{path}"));
}

// Every descriptor's declared label must be a valid metric label value.
BOOST_AUTO_TEST_CASE(route_registry_labels_are_known_values)
{
    for (const auto& route : registeredRoutes()) {
        const bool known = route.route_label == "metrics"
                           || route.route_label == "healthz"
                           || route.route_label == "readyz"
                           || route.route_label == "redirect_prefixed"
                           || route.route_label == "api_links"
                           || route.route_label == "redirect_root"
                           || route.route_label == "app";
        BOOST_TEST(known, "unexpected route_label: " << route.route_label);
        BOOST_TEST(!route.method.empty());
        BOOST_TEST(!route.path_pattern.empty());
        BOOST_TEST(!route.summary.empty());
    }
}

// Registry metadata must agree with the classifier used by request metrics.
BOOST_AUTO_TEST_CASE(route_registry_labels_match_classifier)
{
    for (const auto& route : registeredRoutes()) {
        const auto sample_target = sampleTargetFor(route.path_pattern);
        BOOST_TEST(routeLabelForTarget(sample_target) == route.route_label,
                   "descriptor " << route.method << ' ' << route.path_pattern);
    }
}

BOOST_AUTO_TEST_CASE(route_registry_has_documentation_metadata)
{
    std::set<std::string> operation_ids;
    for (const auto& route : registeredRoutes()) {
        BOOST_TEST(!route.operation_id.empty());
        BOOST_TEST(operation_ids.insert(route.operation_id).second);
        BOOST_TEST(!route.tags.empty());
        BOOST_TEST(!route.responses.empty());

        const auto placeholders = placeholdersFor(route.path_pattern);
        BOOST_TEST(route.path_parameters.size() == placeholders.size());
        for (const auto& param : route.path_parameters) {
            BOOST_TEST(placeholders.count(param.name) == 1U);
            BOOST_TEST(!param.description.empty());
            BOOST_TEST(param.required);
        }
    }
}

BOOST_AUTO_TEST_CASE(route_registry_documents_query_parameters_and_bodies)
{
    const auto* stats = findRoute("GET", "/api/v1/links/{slug}/stats");
    const auto* create = findRoute("POST", "/api/v1/links");
    const auto* compat_create = findRoute("POST", "/api/v1/short-urls");
    const auto* patch = findRoute("PATCH", "/api/v1/links/{slug}");
    const auto* fallback_post = findRoute("POST", "/{path}");
    BOOST_REQUIRE(stats != nullptr);
    BOOST_REQUIRE(create != nullptr);
    BOOST_REQUIRE(compat_create != nullptr);
    BOOST_REQUIRE(patch != nullptr);
    BOOST_REQUIRE(fallback_post != nullptr);

    std::set<std::string> stats_queries;
    for (const auto& param : stats->query_parameters) {
        stats_queries.insert(param.name);
        BOOST_TEST(!param.description.empty());
        BOOST_TEST(!param.required);
    }
    BOOST_TEST(stats_queries.count("from") == 1U);
    BOOST_TEST(stats_queries.count("to") == 1U);
    BOOST_TEST(stats_queries.count("bucket") == 1U);

    BOOST_TEST(!create->request_body_description.empty());
    BOOST_TEST(!compat_create->request_body_description.empty());
    BOOST_TEST(!patch->request_body_description.empty());
    BOOST_TEST(!fallback_post->request_body_description.empty());
}

BOOST_AUTO_TEST_CASE(route_registry_marks_aliases_and_placeholders)
{
    const auto* compat_create = findRoute("POST", "/api/v1/short-urls");
    const auto* compat_read = findRoute("GET", "/api/v1/short-urls/{slug}");
    const auto* prefixed_redirect = findRoute("GET", "/r/{slug}");
    const auto* qr = findRoute("GET", "/api/v1/links/{slug}/qr");
    const auto* routing = findRoute("GET", "/api/v1/links/{slug}/routing");
    BOOST_REQUIRE(compat_create != nullptr);
    BOOST_REQUIRE(compat_read != nullptr);
    BOOST_REQUIRE(prefixed_redirect != nullptr);
    BOOST_REQUIRE(qr != nullptr);
    BOOST_REQUIRE(routing != nullptr);

    BOOST_TEST(compat_create->compatibility_alias);
    BOOST_TEST(compat_read->compatibility_alias);
    BOOST_TEST(!prefixed_redirect->compatibility_alias);
    BOOST_TEST(qr->placeholder);
    BOOST_TEST(routing->placeholder);
}
