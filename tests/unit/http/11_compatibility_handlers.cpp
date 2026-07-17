#define BOOST_TEST_MODULE CompatibilityHandlers
#include <boost/test/unit_test.hpp>

#include <url_shortener/core/config.h>
#include <url_shortener/core/utils.h>
#include <url_shortener/http/RouterBuilder.hpp>
#include <url_shortener/http/handlers/CompatibilityHandlers.hpp>
#include <url_shortener/storage/link_repository.h>

namespace bhttp = boost::beast::http;
using url_shortener::Link;
using url_shortener::http::BeastRequest;
using url_shortener::http::RouteContext;
using url_shortener::http::RouterBuilder;

namespace {

BeastRequest make_request(
    const bhttp::verb method,
    const std::string& target,
    const std::string& body = "{}")
{
    BeastRequest req{method, target, 11};
    req.set(bhttp::field::host, "short.test");
    req.set(bhttp::field::content_type, "application/json");
    req.body() = body;
    req.prepare_payload();
    return req;
}

ServerConfig test_config()
{
    ServerConfig config;
    config.shortener_allow_private_targets = true;
    config.shortener_generated_slug_length = 8;
    return config;
}

Link create_link(const std::string& slug)
{
    Link link;
    link.id = url_shortener::generateId();
    link.slug = slug;
    link.target_url = "https://example.com/" + slug;
    link.created_at = url_shortener::currentTimestamp();
    link.updated_at = link.created_at;
    BOOST_REQUIRE(url_shortener::linkRepository().create(link));
    url_shortener::linkCache().erase(slug);
    return link;
}

boost::beast::http::response<boost::beast::http::string_body> dispatch(
    const BeastRequest& req)
{
    const auto router = RouterBuilder::buildApplicationRouter();
    return router.dispatch(req, test_config(), false);
}

void expect_api_error(
    const boost::beast::http::response<boost::beast::http::string_body>& res,
    const int status,
    const std::string& code)
{
    BOOST_TEST(res.result_int() == status);
    BOOST_TEST(res.body().find("\"code\":\"" + code + "\"") != std::string::npos);
    BOOST_TEST(!res.base()["X-Request-Id"].empty());
}

} // namespace

BOOST_AUTO_TEST_CASE(compat_create_accepts_code_and_generated_slug)
{
    auto with_code = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/short-urls",
        "{\"url\":\"https://example.com/compat\",\"code\":\"chcreate001\"}"));
    BOOST_TEST(with_code.result_int() == 201);
    BOOST_TEST(with_code.body().find("\"slug\":\"chcreate001\"") != std::string::npos);
    BOOST_TEST(std::string(with_code[bhttp::field::content_type]) == "application/json");

    auto generated = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/short-urls",
        "{\"url\":\"https://example.com/generated\"}"));
    BOOST_TEST(generated.result_int() == 201);
    BOOST_TEST(generated.body().find("\"slug\":") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(compat_create_prefers_slug_when_slug_and_code_are_present)
{
    auto res = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/short-urls",
        "{\"url\":\"https://example.com/both\","
        "\"slug\":\"chslugwins001\","
        "\"code\":\"chcodeignored001\"}"));

    BOOST_TEST(res.result_int() == 201);
    BOOST_TEST(res.body().find("\"slug\":\"chslugwins001\"") != std::string::npos);
    BOOST_TEST(res.body().find("chcodeignored001") == std::string::npos);
    BOOST_TEST(!url_shortener::linkRepository().slugExists("chcodeignored001"));
}

BOOST_AUTO_TEST_CASE(compat_create_validates_code)
{
    expect_api_error(
        dispatch(make_request(
            bhttp::verb::post,
            "/api/v1/short-urls",
            "{\"url\":\"https://example.com\",\"code\":\"bad code\"}")),
        400,
        "invalid_slug");

    expect_api_error(
        dispatch(make_request(
            bhttp::verb::post,
            "/api/v1/short-urls",
            "{\"url\":\"https://example.com\",\"code\":\"api\"}")),
        409,
        "reserved_slug");

    create_link("chdupe001");
    expect_api_error(
        dispatch(make_request(
            bhttp::verb::post,
            "/api/v1/short-urls",
            "{\"url\":\"https://example.com\",\"code\":\"chdupe001\"}")),
        409,
        "slug_conflict");
}

BOOST_AUTO_TEST_CASE(compat_read_uses_slug_path_param)
{
    create_link("chread001");

    auto read = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/short-urls/chread001"));
    BOOST_TEST(read.result_int() == 200);
    BOOST_TEST(read.body().find("\"slug\":\"chread001\"") != std::string::npos);
    BOOST_TEST(std::string(read[bhttp::field::content_type]) == "application/json");

    expect_api_error(
        dispatch(make_request(
            bhttp::verb::get,
            "/api/v1/short-urls/chmissing001")),
        404,
        "not_found");
}

BOOST_AUTO_TEST_CASE(compat_router_preserves_wrong_method_shape)
{
    expect_api_error(
        dispatch(make_request(
            bhttp::verb::get,
            "/api/v1/short-urls")),
        400,
        "invalid_method");

    expect_api_error(
        dispatch(make_request(
            bhttp::verb::post,
            "/api/v1/short-urls/chread001")),
        400,
        "invalid_method");
}

BOOST_AUTO_TEST_CASE(compat_read_missing_context_slug_fails_as_not_found)
{
    RouteContext context;
    const auto res = url_shortener::http::handleCompatGetLinkBySlug(
        make_request(bhttp::verb::get, "/api/v1/short-urls/"),
        test_config(),
        false,
        context);

    expect_api_error(res, 404, "not_found");
}
