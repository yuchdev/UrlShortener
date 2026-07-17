#define BOOST_TEST_MODULE RouteContext
#include <boost/test/unit_test.hpp>

#include <url_shortener/http/RouteContext.hpp>

using url_shortener::http::RouteContext;
using url_shortener::http::pathParam;

BOOST_AUTO_TEST_CASE(empty_context_has_no_params)
{
    const RouteContext context;
    BOOST_TEST(context.path_params.empty());
    BOOST_TEST(context.query_string.empty());
    BOOST_TEST(context.route_label.empty());
    BOOST_TEST(!pathParam(context, "slug").has_value());
}

BOOST_AUTO_TEST_CASE(path_param_returns_existing_value)
{
    RouteContext context;
    context.path_params["slug"] = "docs";
    context.query_string = "from=1&to=2";

    const auto slug = pathParam(context, "slug");
    BOOST_REQUIRE(slug.has_value());
    BOOST_TEST(*slug == "docs");
    BOOST_TEST(!pathParam(context, "id").has_value());
    BOOST_TEST(context.query_string == "from=1&to=2");
}
