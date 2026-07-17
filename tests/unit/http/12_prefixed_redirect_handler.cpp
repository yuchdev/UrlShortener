#define BOOST_TEST_MODULE PrefixedRedirectHandler
#include <boost/test/unit_test.hpp>

#include <url_shortener/core/config.h>
#include <url_shortener/core/utils.h>
#include <url_shortener/analytics/AggregateQuery.hpp>
#include <url_shortener/analytics/AggregateStats.hpp>
#include <url_shortener/http/RouterBuilder.hpp>
#include <url_shortener/storage/memory/InMemoryClickEventRepository.hpp>
#include <url_shortener/storage/link_repository.h>

#include <chrono>

namespace bhttp = boost::beast::http;
using url_shortener::Link;
using url_shortener::LinkStatus;
using url_shortener::RedirectType;
using url_shortener::http::BeastRequest;
using url_shortener::http::RouterBuilder;

namespace url_shortener {
storage::memory::InMemoryClickEventRepository& analyticsClickRepository();
}

namespace {

BeastRequest make_request(const bhttp::verb method, const std::string& target)
{
    BeastRequest req{method, target, 11};
    req.set(bhttp::field::host, "short.test");
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

std::uint64_t aggregate_attempts_for(const std::string& slug, const std::uint16_t status)
{
    url_shortener::analytics::AggregateQuery query;
    query.slug = slug;
    query.from = std::chrono::system_clock::now() - std::chrono::hours(1);
    query.to = std::chrono::system_clock::now() + std::chrono::hours(1);
    url_shortener::analytics::AggregateStats stats;
    BOOST_REQUIRE(url_shortener::analyticsClickRepository().GetAggregateStats(query, &stats, nullptr));
    for (const auto& count : stats.status_code_counts) {
        if (count.status_code == status) {
            return count.count;
        }
    }
    return 0;
}

void expect_redirect(
    const boost::beast::http::response<boost::beast::http::string_body>& res,
    const int status,
    const std::string& location)
{
    BOOST_TEST(res.result_int() == status);
    BOOST_TEST(std::string(res[bhttp::field::location]) == location);
    BOOST_TEST(!res.keep_alive());
    BOOST_TEST(res.body().find("\"slug\"") == std::string::npos);
    BOOST_TEST(!res.base()["X-Request-Id"].empty());
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

BOOST_AUTO_TEST_CASE(prefixed_redirect_returns_temporary_and_permanent_redirects)
{
    create_link("prtemp001", RedirectType::temporary);
    create_link("prperm001", RedirectType::permanent);

    expect_redirect(
        dispatch(make_request(bhttp::verb::get, "/r/prtemp001")),
        302,
        "https://example.com/prtemp001");
    expect_redirect(
        dispatch(make_request(bhttp::verb::get, "/r/prperm001")),
        301,
        "https://example.com/prperm001");

    const auto updated = url_shortener::linkRepository().getBySlug("prtemp001");
    BOOST_REQUIRE(updated.has_value());
    BOOST_TEST(updated->stats.total_redirects == 1U);
    BOOST_TEST(updated->stats.last_accessed_at.has_value());
}

BOOST_AUTO_TEST_CASE(prefixed_redirect_ignores_query_string_for_slug_lookup)
{
    create_link("prquery001", RedirectType::temporary);

    expect_redirect(
        dispatch(make_request(bhttp::verb::get, "/r/prquery001?utm_source=test")),
        302,
        "https://example.com/prquery001");
}

BOOST_AUTO_TEST_CASE(prefixed_redirect_validates_slug_and_missing_link)
{
    expect_error(dispatch(make_request(bhttp::verb::get, "/r/bad.slug")), 404, "not_found");
    expect_error(dispatch(make_request(bhttp::verb::get, "/r/prmissing001")), 404, "not_found");
}

BOOST_AUTO_TEST_CASE(prefixed_redirect_reports_terminal_link_states)
{
    auto deleted = create_link("prdeleted001");
    deleted.deleted_at = url_shortener::currentTimestamp();
    BOOST_REQUIRE(url_shortener::updateLinkAndInvalidateCache(deleted));

    auto disabled = create_link("prdisabled001");
    disabled.enabled = false;
    BOOST_REQUIRE(url_shortener::updateLinkAndInvalidateCache(disabled));

    auto expired = create_link("prexpired001");
    expired.expires_at = "2000-01-01T00:00:00Z";
    BOOST_REQUIRE(url_shortener::updateLinkAndInvalidateCache(expired));

    expect_error(dispatch(make_request(bhttp::verb::get, "/r/prdeleted001")), 404, "link_deleted");
    expect_error(dispatch(make_request(bhttp::verb::get, "/r/prdisabled001")), 410, "link_disabled");
    expect_error(dispatch(make_request(bhttp::verb::get, "/r/prexpired001")), 410, "link_expired");
}

BOOST_AUTO_TEST_CASE(prefixed_redirect_analytics_can_be_disabled)
{
    auto config = test_config();
    config.analytics_enabled = false;
    create_link("prnoanalytics001");

    expect_redirect(
        dispatch_with_config(make_request(bhttp::verb::get, "/r/prnoanalytics001"), config),
        302,
        "https://example.com/prnoanalytics001");
    BOOST_TEST(aggregate_attempts_for("prnoanalytics001", 302) == 0U);
}

BOOST_AUTO_TEST_CASE(prefixed_redirect_records_terminal_status_analytics)
{
    auto config = test_config();
    config.analytics_enabled = true;

    auto disabled = create_link("pranalyticsdisabled001");
    disabled.enabled = false;
    BOOST_REQUIRE(url_shortener::updateLinkAndInvalidateCache(disabled));

    auto expired = create_link("pranalyticsexpired001");
    expired.expires_at = "2000-01-01T00:00:00Z";
    BOOST_REQUIRE(url_shortener::updateLinkAndInvalidateCache(expired));

    expect_error(
        dispatch_with_config(make_request(bhttp::verb::get, "/r/pranalyticsdisabled001"), config),
        410,
        "link_disabled");
    expect_error(
        dispatch_with_config(make_request(bhttp::verb::get, "/r/pranalyticsexpired001"), config),
        410,
        "link_expired");

    BOOST_TEST(aggregate_attempts_for("pranalyticsdisabled001", 410) == 1U);
    BOOST_TEST(aggregate_attempts_for("pranalyticsexpired001", 410) == 1U);
}

BOOST_AUTO_TEST_CASE(prefixed_redirect_wrong_method_uses_router_error_shape)
{
    expect_error(dispatch(make_request(bhttp::verb::post, "/r/prmissing001")), 400, "invalid_method");
}
