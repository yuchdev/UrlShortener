#define BOOST_TEST_MODULE RouterRegistryConsistency
#include <boost/test/unit_test.hpp>

#include <url_shortener/http/RouteRegistry.hpp>
#include <url_shortener/http/RouterBuilder.hpp>

#include <set>
#include <string>

using url_shortener::http::registeredRoutes;
using url_shortener::http::RouteEntryView;
using url_shortener::http::RouterBuilder;

namespace {

std::string key(const std::string& method, const std::string& path)
{
    return method + " " + path;
}

std::string key(const RouteEntryView& route)
{
    return key(route.method, route.path_pattern);
}

} // namespace

BOOST_AUTO_TEST_CASE(router_and_registry_have_same_routes)
{
    const auto router = RouterBuilder::buildApplicationRouter();
    const auto router_routes = router.routes();
    const auto& registry_routes = registeredRoutes();

    std::set<std::string> router_keys;
    for (const auto& route : router_routes) {
        const auto inserted = router_keys.insert(key(route)).second;
        BOOST_TEST(inserted, "duplicate router route " << key(route));
    }

    std::set<std::string> registry_keys;
    for (const auto& route : registry_routes) {
        const auto inserted = registry_keys.insert(key(route.method, route.path_pattern)).second;
        BOOST_TEST(inserted, "duplicate registry route " << key(route.method, route.path_pattern));
    }

    BOOST_TEST(router_keys.size() == registry_keys.size());
    for (const auto& registry_route : registry_routes) {
        const auto route_key = key(registry_route.method, registry_route.path_pattern);
        BOOST_TEST(router_keys.count(route_key) == 1U, "missing router route " << route_key);
    }
    for (const auto& router_route : router_routes) {
        const auto route_key = key(router_route);
        BOOST_TEST(registry_keys.count(route_key) == 1U, "missing registry route " << route_key);
    }
}

BOOST_AUTO_TEST_CASE(router_and_registry_labels_match)
{
    const auto router = RouterBuilder::buildApplicationRouter();
    const auto router_routes = router.routes();

    for (const auto& registry_route : registeredRoutes()) {
        bool found = false;
        for (const auto& router_route : router_routes) {
            if (registry_route.method == router_route.method
                && registry_route.path_pattern == router_route.path_pattern) {
                found = true;
                BOOST_TEST(router_route.route_label == registry_route.route_label);
            }
        }
        BOOST_TEST(found, "missing route " << registry_route.method << ' ' << registry_route.path_pattern);
    }
}
