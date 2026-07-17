#define BOOST_TEST_MODULE LinkHandlers
#include <boost/test/unit_test.hpp>

#include <url_shortener/core/config.h>
#include <url_shortener/core/utils.h>
#include <url_shortener/http/RouterBuilder.hpp>
#include <url_shortener/storage/link_repository.h>

namespace bhttp = boost::beast::http;
using url_shortener::Link;
using url_shortener::RedirectType;
using url_shortener::http::BeastRequest;
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

Link create_link(
    const std::string& slug,
    const RedirectType redirect_type = RedirectType::temporary)
{
    Link link;
    link.id = url_shortener::generateId();
    link.slug = slug;
    link.target_url = "https://example.com/" + slug;
    link.created_at = url_shortener::currentTimestamp();
    link.updated_at = link.created_at;
    link.redirect_type = redirect_type;
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

boost::beast::http::response<boost::beast::http::string_body> dispatch_with_config(
    const BeastRequest& req,
    const ServerConfig& config)
{
    const auto router = RouterBuilder::buildApplicationRouter();
    return router.dispatch(req, config, false);
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

BOOST_AUTO_TEST_CASE(create_custom_slug_and_read_by_id_and_slug)
{
    auto create = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/links",
        "{\"url\":\"https://example.com/new\",\"slug\":\"lhcreate001\"}"));
    BOOST_TEST(create.result_int() == 201);
    BOOST_TEST(create.body().find("\"slug\":\"lhcreate001\"") != std::string::npos);
    BOOST_TEST(std::string(create[bhttp::field::content_type]) == "application/json");

    const auto stored = url_shortener::linkRepository().getBySlug("lhcreate001");
    BOOST_REQUIRE(stored.has_value());

    auto by_id = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/id/" + stored->id));
    BOOST_TEST(by_id.result_int() == 200);
    BOOST_TEST(by_id.body().find("\"slug\":\"lhcreate001\"") != std::string::npos);

    auto by_slug = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhcreate001"));
    BOOST_TEST(by_slug.result_int() == 200);
    BOOST_TEST(by_slug.body().find("\"slug\":\"lhcreate001\"") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(create_generated_slug_and_validation_errors)
{
    auto generated = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/links",
        "{\"url\":\"https://example.com/generated\"}"));
    BOOST_TEST(generated.result_int() == 201);
    BOOST_TEST(generated.body().find("\"slug\":") != std::string::npos);

    expect_api_error(
        dispatch(make_request(bhttp::verb::post, "/api/v1/links", "{}")),
        400,
        "invalid_url");

    expect_api_error(
        dispatch(make_request(
            bhttp::verb::post,
            "/api/v1/links",
            "{\"url\":\"https://example.com\",\"slug\":\"bad slug\"}")),
        400,
        "invalid_slug");

    expect_api_error(
        dispatch(make_request(
            bhttp::verb::post,
            "/api/v1/links",
            "{\"url\":\"https://example.com\",\"slug\":\"api\"}")),
        409,
        "reserved_slug");

    create_link("lhdupe001");
    expect_api_error(
        dispatch(make_request(
            bhttp::verb::post,
            "/api/v1/links",
            "{\"url\":\"https://example.com\",\"slug\":\"lhdupe001\"}")),
        409,
        "slug_conflict");
}

BOOST_AUTO_TEST_CASE(create_rejects_private_targets_when_disabled)
{
    ServerConfig config;
    config.shortener_allow_private_targets = false;
    config.shortener_generated_slug_length = 8;

    expect_api_error(
        dispatch_with_config(
            make_request(
                bhttp::verb::post,
                "/api/v1/links",
                "{\"url\":\"http://127.0.0.1/internal\",\"slug\":\"lhprivate001\"}"),
            config),
        400,
        "invalid_url");
}

BOOST_AUTO_TEST_CASE(patch_delete_preview_and_actions)
{
    create_link("lhpatch001");
    create_link("lhdelete001");
    create_link("lhpreview001");
    create_link("lhaction001");
    auto deleted = create_link("lhrestore001");
    deleted.deleted_at = url_shortener::currentTimestamp();
    BOOST_REQUIRE(url_shortener::updateLinkAndInvalidateCache(deleted));

    auto patch = dispatch(make_request(
        bhttp::verb::patch,
        "/api/v1/links/lhpatch001",
        "{\"enabled\":false,\"tags\":[\"docs\"],"
        "\"metadata\":{\"team\":\"core\"},"
        "\"campaign\":{\"name\":\"spring\"}}"));
    BOOST_TEST(patch.result_int() == 200);
    BOOST_TEST(patch.body().find("\"status\":\"disabled\"") != std::string::npos);
    BOOST_TEST(patch.body().find("\"docs\"") != std::string::npos);
    BOOST_TEST(patch.body().find("\"team\":\"core\"") != std::string::npos);
    BOOST_TEST(patch.body().find("\"name\":\"spring\"") != std::string::npos);

    expect_api_error(
        dispatch(make_request(
            bhttp::verb::patch,
            "/api/v1/links/lhpatch001",
            "{\"enabled\":\"false\"}")),
        400,
        "invalid_enabled");

    auto remove = dispatch(make_request(
        bhttp::verb::delete_,
        "/api/v1/links/lhdelete001"));
    BOOST_TEST(remove.result_int() == 200);
    BOOST_TEST(remove.body().find("\"status\":\"deleted\"") != std::string::npos);

    auto preview = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhpreview001/preview"));
    BOOST_TEST(preview.result_int() == 200);
    BOOST_TEST(preview.body().find("\"status\":\"active\"") != std::string::npos);

    auto disable = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/links/lhaction001/disable"));
    BOOST_TEST(disable.result_int() == 200);
    BOOST_TEST(disable.body().find("\"status\":\"disabled\"") != std::string::npos);

    auto enable = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/links/lhaction001/enable"));
    BOOST_TEST(enable.result_int() == 200);
    BOOST_TEST(enable.body().find("\"status\":\"active\"") != std::string::npos);

    auto restore = dispatch(make_request(
        bhttp::verb::post,
        "/api/v1/links/lhrestore001/restore"));
    BOOST_TEST(restore.result_int() == 200);
    BOOST_TEST(restore.body().find("\"status\":\"active\"") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(stats_and_placeholder_routes)
{
    auto link = create_link("lhstats001");
    link.stats.total_redirects = 3;
    link.stats.redirects_24h = 2;
    link.stats.redirects_7d = 3;
    BOOST_REQUIRE(url_shortener::updateLinkAndInvalidateCache(link));
    create_link("lhplaceholder001");

    auto stats = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhstats001/stats"));
    BOOST_TEST(stats.result_int() == 200);
    BOOST_TEST(stats.body().find("\"total_redirects\":3") != std::string::npos);

    auto invalid_stats = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhstats001/stats?from=x&to=2&bucket=day"));
    BOOST_TEST(invalid_stats.result_int() == 400);
    BOOST_TEST(invalid_stats.body().find("invalid_timestamp") != std::string::npos);
    BOOST_TEST(!invalid_stats.base()["X-Request-Id"].empty());

    auto qr = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhplaceholder001/qr"));
    expect_api_error(qr, 501, "feature_not_enabled");

    auto routing = dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/lhplaceholder001/routing"));
    expect_api_error(routing, 501, "feature_not_enabled");

    expect_api_error(
        dispatch(make_request(
            bhttp::verb::get,
            "/api/v1/links/lhmissing001/qr")),
        404,
        "not_found");
}
