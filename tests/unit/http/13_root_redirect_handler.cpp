#define BOOST_TEST_MODULE RootRedirectHandler
#include <boost/test/unit_test.hpp>

#include <url_shortener/core/config.h>
#include <url_shortener/core/utils.h>
#include <url_shortener/http/RouterBuilder.hpp>
#include <url_shortener/http/request_handlers.h>
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

boost::beast::http::response<boost::beast::http::string_body> dispatch_router(
    const BeastRequest& req)
{
    const auto router = RouterBuilder::buildApplicationRouter();
    return router.dispatch(req, test_config(), false);
}

void expect_error(
    const boost::beast::http::response<boost::beast::http::string_body>& res,
    const int status,
    const std::string& code)
{
    BOOST_TEST(res.result_int() == status);
    BOOST_TEST(res.body().find("\"code\":\"" + code + "\"") != std::string::npos);
    BOOST_TEST(!res.base()["X-Request-Id"].empty());
}

} // namespace

BOOST_AUTO_TEST_CASE(root_redirect_returns_active_link_redirect)
{
    create_link("rractive001");
    create_link("rrperm001", RedirectType::permanent);

    const auto res = dispatch_router(make_request(bhttp::verb::get, "/rractive001"));
    BOOST_TEST(res.result_int() == 302);
    BOOST_TEST(std::string(res[bhttp::field::location]) == "https://example.com/rractive001");
    BOOST_TEST(!res.keep_alive());
    BOOST_TEST(res.body().find("\"slug\"") == std::string::npos);

    const auto updated = url_shortener::linkRepository().getBySlug("rractive001");
    BOOST_REQUIRE(updated.has_value());
    BOOST_TEST(updated->stats.total_redirects == 1U);

    const auto permanent = dispatch_router(make_request(bhttp::verb::get, "/rrperm001"));
    BOOST_TEST(permanent.result_int() == 301);
    BOOST_TEST(std::string(permanent[bhttp::field::location]) == "https://example.com/rrperm001");
}

BOOST_AUTO_TEST_CASE(root_redirect_reports_terminal_and_missing_states)
{
    auto deleted = create_link("rrdeleted001");
    deleted.deleted_at = url_shortener::currentTimestamp();
    BOOST_REQUIRE(url_shortener::updateLinkAndInvalidateCache(deleted));

    auto disabled = create_link("rrdisabled001");
    disabled.enabled = false;
    BOOST_REQUIRE(url_shortener::updateLinkAndInvalidateCache(disabled));

    auto expired = create_link("rrexpired001");
    expired.expires_at = "2000-01-01T00:00:00Z";
    BOOST_REQUIRE(url_shortener::updateLinkAndInvalidateCache(expired));

    expect_error(dispatch_router(make_request(bhttp::verb::get, "/rrdeleted001")), 404, "link_deleted");
    expect_error(dispatch_router(make_request(bhttp::verb::get, "/rrdisabled001")), 410, "link_disabled");
    expect_error(dispatch_router(make_request(bhttp::verb::get, "/rrexpired001")), 410, "link_expired");
    expect_error(dispatch_router(make_request(bhttp::verb::get, "/rrmissing001")), 404, "not_found");
}

BOOST_AUTO_TEST_CASE(root_redirect_falls_back_for_invalid_slug_and_non_get)
{
    const auto config = test_config();

    const auto invalid_slug = url_shortener::handleShortenerRequest(
        make_request(bhttp::verb::get, "/bad.slug"),
        config,
        false);
    BOOST_TEST(invalid_slug.result_int() == 404);
    BOOST_TEST(invalid_slug.body() == "URI not found\n");

    const auto non_get = url_shortener::handleShortenerRequest(
        make_request(bhttp::verb::post, "/rrpostfallback", "stored body", "text/plain"),
        config,
        false);
    BOOST_TEST(non_get.result_int() == 200);
    BOOST_TEST(non_get.body() == "URI saved\n");
}

BOOST_AUTO_TEST_CASE(root_redirect_with_query_falls_back_to_uri_store)
{
    const auto config = test_config();

    const auto post = url_shortener::handleShortenerRequest(
        make_request(bhttp::verb::post, "/rrquery001?utm=1", "query body", "text/plain"),
        config,
        false);
    BOOST_TEST(post.result_int() == 200);

    const auto get = url_shortener::handleShortenerRequest(
        make_request(bhttp::verb::get, "/rrquery001?utm=1"),
        config,
        false);
    BOOST_TEST(get.result_int() == 200);
    BOOST_TEST(get.body() == "query body\n");
}

BOOST_AUTO_TEST_CASE(api_targets_are_not_handled_as_root_redirects)
{
    const auto res = url_shortener::handleShortenerRequest(
        make_request(bhttp::verb::get, "/api/not-a-link"),
        test_config(),
        false);

    BOOST_TEST(res.result_int() == 404);
    BOOST_TEST(res.body().find("\"code\":\"not_found\"") != std::string::npos);
    BOOST_TEST(std::string(res[bhttp::field::content_type]) == "application/json");
}
