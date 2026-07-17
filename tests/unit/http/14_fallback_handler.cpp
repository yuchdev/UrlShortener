#define BOOST_TEST_MODULE FallbackHandler
#include <boost/test/unit_test.hpp>

#include <url_shortener/core/config.h>
#include <url_shortener/http/RouteContext.hpp>
#include <url_shortener/http/RouterBuilder.hpp>
#include <url_shortener/http/handlers/FallbackHandlers.hpp>

namespace bhttp = boost::beast::http;
using url_shortener::http::BeastRequest;
using url_shortener::http::RouteContext;
using url_shortener::http::RouterBuilder;

namespace {

BeastRequest make_request(
    const bhttp::verb method,
    const std::string& target,
    const std::string& body = "{}",
    const std::string& content_type = "application/json")
{
    BeastRequest req{method, target, 11};
    req.set(bhttp::field::host, "short.test");
    req.set(bhttp::field::content_type, content_type);
    req.body() = body;
    req.prepare_payload();
    return req;
}

ServerConfig test_config()
{
    ServerConfig config;
    config.shortener_allow_private_targets = true;
    return config;
}

boost::beast::http::response<boost::beast::http::string_body> dispatch(
    const BeastRequest& req)
{
    const auto router = RouterBuilder::buildApplicationRouter();
    return router.dispatch(req, test_config(), false);
}

} // namespace

BOOST_AUTO_TEST_CASE(fallback_uri_store_roundtrip_uses_router_handler)
{
    RouteContext context;
    const auto post = url_shortener::http::handleFallbackUriStore(
        make_request(bhttp::verb::post, "/fallback-doc", "stored fallback", "text/plain"),
        test_config(),
        false,
        context);
    BOOST_TEST(post.result_int() == 200);
    BOOST_TEST(post.body() == "URI saved\n");

    const auto get = url_shortener::http::handleFallbackUriStore(
        make_request(bhttp::verb::get, "/fallback-doc"),
        test_config(),
        false,
        context);
    BOOST_TEST(get.result_int() == 200);
    BOOST_TEST(get.body() == "stored fallback\n");
    BOOST_TEST(std::string(get[bhttp::field::content_type]) == "text/plain");

    const auto remove = url_shortener::http::handleFallbackUriStore(
        make_request(bhttp::verb::delete_, "/fallback-doc"),
        test_config(),
        false,
        context);
    BOOST_TEST(remove.result_int() == 200);
    BOOST_TEST(remove.body() == "Data deleted\n");

    const auto after_delete = url_shortener::http::handleFallbackUriStore(
        make_request(bhttp::verb::get, "/fallback-doc"),
        test_config(),
        false,
        context);
    BOOST_TEST(after_delete.result_int() == 404);
    BOOST_TEST(after_delete.body() == "URI not found\n");
}

BOOST_AUTO_TEST_CASE(fallback_handler_preserves_unsupported_method_shape)
{
    RouteContext context;
    const auto res = url_shortener::http::handleFallbackUriStore(
        make_request(bhttp::verb::put, "/fallback-doc"),
        test_config(),
        false,
        context);

    BOOST_TEST(res.result_int() == 400);
    BOOST_TEST(res.body() == "Invalid method\n");
    BOOST_TEST(std::string(res[bhttp::field::content_type]) == "text/plain");
}

BOOST_AUTO_TEST_CASE(fallback_route_handles_nested_paths)
{
    const auto post = dispatch(make_request(
        bhttp::verb::post,
        "/fallback/nested/doc",
        "nested fallback",
        "text/plain"));
    BOOST_TEST(post.result_int() == 200);

    const auto get = dispatch(make_request(bhttp::verb::get, "/fallback/nested/doc"));
    BOOST_TEST(get.result_int() == 200);
    BOOST_TEST(get.body() == "nested fallback\n");
}

BOOST_AUTO_TEST_CASE(valid_slug_fallback_asymmetry_is_explicit)
{
    const auto post = dispatch(make_request(
        bhttp::verb::post,
        "/fbslug001",
        "single segment fallback",
        "text/plain"));
    BOOST_TEST(post.result_int() == 200);

    const auto get = dispatch(make_request(bhttp::verb::get, "/fbslug001"));
    BOOST_TEST(get.result_int() == 404);
    BOOST_TEST(get.body().find("\"code\":\"not_found\"") != std::string::npos);
    BOOST_TEST(std::string(get[bhttp::field::content_type]) == "application/json");
}

BOOST_AUTO_TEST_CASE(fallback_routes_do_not_steal_api_or_redirect_targets)
{
    const auto api_get = dispatch(make_request(bhttp::verb::get, "/api/v1/links/foo/bar"));
    BOOST_TEST(api_get.result_int() == 404);
    BOOST_TEST(api_get.body().find("\"code\":\"not_found\"") != std::string::npos);
    BOOST_TEST(std::string(api_get[bhttp::field::content_type]) == "application/json");

    const auto api_post = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/links/foo/bar",
        "must not store",
        "text/plain"));
    BOOST_TEST(api_post.result_int() == 404);
    BOOST_TEST(api_post.body().find("\"code\":\"not_found\"") != std::string::npos);

    const auto redirect_get = dispatch(make_request(bhttp::verb::get, "/r/foo/bar"));
    BOOST_TEST(redirect_get.result_int() == 404);
    BOOST_TEST(redirect_get.body().find("\"code\":\"not_found\"") != std::string::npos);

    const auto redirect_post = dispatch(make_request(
        bhttp::verb::post,
        "/r/foo/bar",
        "must not store",
        "text/plain"));
    BOOST_TEST(redirect_post.result_int() == 404);
    BOOST_TEST(redirect_post.body().find("\"code\":\"not_found\"") != std::string::npos);
}
