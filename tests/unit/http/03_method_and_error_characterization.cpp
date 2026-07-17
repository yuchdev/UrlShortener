/** @file 03_method_and_error_characterization.cpp
 *  @brief Characterizes current wrong-method and validation error responses.
 */
#define BOOST_TEST_MODULE HttpMethodAndErrorCharacterization
#include <boost/test/unit_test.hpp>

#include <url_shortener/url_shortener.h>

#include <string>

using namespace url_shortener;
namespace bhttp = boost::beast::http;

namespace {

bhttp::request<bhttp::string_body> make_request(
    const bhttp::verb method,
    const std::string& target,
    const std::string& body = "{}")
{
    bhttp::request<bhttp::string_body> req{method, target, 11};
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
    return config;
}

Link create_link(const std::string& slug)
{
    Link link;
    link.id = generateId();
    link.slug = slug;
    link.target_url = "https://example.com/" + slug;
    link.created_at = currentTimestamp();
    link.updated_at = link.created_at;
    BOOST_REQUIRE(linkRepository().create(link));
    linkCache().erase(slug);
    return link;
}

void expect_api_error(
    const bhttp::response<bhttp::string_body>& res,
    const int status,
    const std::string& code,
    const std::string& message_fragment = "")
{
    BOOST_TEST(res.result_int() == status);
    BOOST_TEST(res.body().find("\"code\":\"" + code + "\"")
               != std::string::npos);
    if (!message_fragment.empty()) {
        BOOST_TEST(res.body().find(message_fragment) != std::string::npos);
    }
    BOOST_TEST(!res.base()["X-Request-Id"].empty());
}

} // namespace

/**
 * [Unit][HTTP] Wrong methods keep current 400 invalid_method responses.
 */
BOOST_AUTO_TEST_CASE(wrong_methods_return_current_invalid_method_shape)
{
    const auto config = test_config();
    create_link("mepreview001");
    create_link("meenable001");
    create_link("meredirect001");

    expect_api_error(
        handleShortenerRequest(
            make_request(bhttp::verb::post, "/healthz"), config, false),
        400,
        "invalid_method",
        "Only GET is supported");

    expect_api_error(
        handleShortenerRequest(
            make_request(bhttp::verb::post, "/readyz"), config, false),
        400,
        "invalid_method",
        "Only GET is supported");

    expect_api_error(
        handleShortenerRequest(
            make_request(bhttp::verb::post, "/metrics"), config, false),
        400,
        "invalid_method",
        "Only GET is supported");

    expect_api_error(
        handleShortenerRequest(
            make_request(bhttp::verb::post, "/api/v1/links/id/unknown"),
            config,
            false),
        400,
        "invalid_method");

    expect_api_error(
        handleShortenerRequest(
            make_request(
                bhttp::verb::post,
                "/api/v1/links/mepreview001/preview"),
            config,
            false),
        400,
        "invalid_method");

    expect_api_error(
        handleShortenerRequest(
            make_request(bhttp::verb::get, "/api/v1/links/meenable001/enable"),
            config,
            false),
        400,
        "invalid_method");

    expect_api_error(
        handleShortenerRequest(
            make_request(bhttp::verb::post, "/r/meredirect001"),
            config,
            false),
        400,
        "invalid_method");
}

/**
 * [Unit][HTTP] Create validation errors keep current status and error codes.
 */
BOOST_AUTO_TEST_CASE(create_validation_errors_are_characterized)
{
    const auto config = test_config();

    expect_api_error(
        handleShortenerRequest(
            make_request(bhttp::verb::post, "/api/v1/links", "{}"),
            config,
            false),
        400,
        "invalid_url");

    expect_api_error(
        handleShortenerRequest(
            make_request(
                bhttp::verb::post,
                "/api/v1/links",
                "{\"url\":\"not-a-url\"}"),
            config,
            false),
        400,
        "invalid_url");

    expect_api_error(
        handleShortenerRequest(
            make_request(
                bhttp::verb::post,
                "/api/v1/links",
                "{\"url\":\"https://example.com\",\"slug\":\"bad slug\"}"),
            config,
            false),
        400,
        "invalid_slug");

    expect_api_error(
        handleShortenerRequest(
            make_request(
                bhttp::verb::post,
                "/api/v1/links",
                "{\"url\":\"https://example.com\",\"slug\":\"api\"}"),
            config,
            false),
        409,
        "reserved_slug");
}

/**
 * [Unit][HTTP] Patch validation errors keep current status and error codes.
 */
BOOST_AUTO_TEST_CASE(patch_validation_errors_are_characterized)
{
    const auto config = test_config();
    create_link("mepatch001");
    create_link("mepatch002");

    expect_api_error(
        handleShortenerRequest(
            make_request(
                bhttp::verb::patch,
                "/api/v1/links/mepatch001",
                "{\"enabled\":\"false\"}"),
            config,
            false),
        400,
        "invalid_enabled");

    expect_api_error(
        handleShortenerRequest(
            make_request(
                bhttp::verb::patch,
                "/api/v1/links/mepatch002",
                "{\"expires_at\":\"tomorrow\"}"),
            config,
            false),
        400,
        "invalid_expires_at");
}

/**
 * [Unit][HTTP] Stats query validation keeps current handler errors.
 */
BOOST_AUTO_TEST_CASE(stats_query_validation_errors_are_characterized)
{
    const auto config = test_config();
    create_link("mestats001");
    create_link("mestats002");

    auto invalid_time = handleShortenerRequest(
        make_request(
            bhttp::verb::get,
            "/api/v1/links/mestats001/stats?from=x&to=2&bucket=day"),
        config,
        false);
    BOOST_TEST(invalid_time.result_int() == 400);
    BOOST_TEST(invalid_time.body().find("invalid_timestamp")
               != std::string::npos);
    BOOST_TEST(!invalid_time.base()["X-Request-Id"].empty());

    auto invalid_bucket = handleShortenerRequest(
        make_request(
            bhttp::verb::get,
            "/api/v1/links/mestats002/stats?from=1&to=2&bucket=bad"),
        config,
        false);
    BOOST_TEST(invalid_bucket.result_int() == 400);
    BOOST_TEST(invalid_bucket.body().find("invalid_bucket")
               != std::string::npos);
    BOOST_TEST(!invalid_bucket.base()["X-Request-Id"].empty());
}
