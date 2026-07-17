#define BOOST_TEST_MODULE RouterMatcher
#include <boost/test/unit_test.hpp>

#include <url_shortener/http/Router.hpp>

using url_shortener::http::RouteContext;
using url_shortener::http::Router;

BOOST_AUTO_TEST_CASE(matches_exact_path)
{
    RouteContext context;
    BOOST_TEST(Router::matchPath("/healthz", "/healthz", &context));
    BOOST_TEST(context.path_params.empty());
    BOOST_TEST(context.query_string.empty());
}

BOOST_AUTO_TEST_CASE(matches_parameter_path)
{
    RouteContext context;
    BOOST_TEST(Router::matchPath("/api/v1/links/{slug}", "/api/v1/links/docs", &context));
    BOOST_TEST(context.path_params["slug"] == "docs");
}

BOOST_AUTO_TEST_CASE(strips_query_string)
{
    RouteContext context;
    BOOST_TEST(Router::matchPath(
        "/api/v1/links/{slug}/stats",
        "/api/v1/links/docs/stats?from=1&to=2&bucket=day",
        &context));
    BOOST_TEST(context.path_params["slug"] == "docs");
    BOOST_TEST(context.query_string == "from=1&to=2&bucket=day");
}

BOOST_AUTO_TEST_CASE(rejects_mismatches)
{
    RouteContext context;
    BOOST_TEST(!Router::matchPath("/api/v1/links/{slug}", "/api/v1/links/docs/stats", &context));
    BOOST_TEST(!Router::matchPath("/readyz", "/healthz", &context));
    BOOST_TEST(!Router::matchPath("/{slug}", "/api/v1/links", &context));
}

BOOST_AUTO_TEST_CASE(rejects_empty_capture)
{
    RouteContext context;
    BOOST_TEST(!Router::matchPath("/r/{slug}", "/r/", &context));
}

BOOST_AUTO_TEST_CASE(matches_root_path)
{
    RouteContext context;
    BOOST_TEST(Router::matchPath("/", "/", &context));
    BOOST_TEST(context.path_params.empty());
}

BOOST_AUTO_TEST_CASE(null_context_never_matches)
{
    BOOST_TEST(!Router::matchPath("/healthz", "/healthz", nullptr));
}
