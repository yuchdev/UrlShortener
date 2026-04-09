#define BOOST_TEST_MODULE LinkManagementStatsIncrementTest
#include <boost/test/unit_test.hpp>
#include "../../../src/url_shortener.cpp"

BOOST_AUTO_TEST_CASE(stats_increment_on_successful_redirect_only)
{
    ServerConfig config;
    config.shortener_allow_private_targets = true;

    Link active;
    active.id = generateId();
    active.slug = "lmstat01";
    active.target_url = "https://example.com/a";
    active.created_at = currentTimestamp();
    active.updated_at = active.created_at;
    BOOST_REQUIRE(linkRepository().create(active));

    Link disabled;
    disabled.id = generateId();
    disabled.slug = "lmstat02";
    disabled.target_url = "https://example.com/b";
    disabled.created_at = currentTimestamp();
    disabled.updated_at = disabled.created_at;
    disabled.enabled = false;
    BOOST_REQUIRE(linkRepository().create(disabled));

    http::request<http::string_body> ok_req{http::verb::get, "/lmstat01", 11};
    const auto ok_res = handleShortenerRequest(ok_req, config, false);
    BOOST_TEST(ok_res.result_int() == 302);

    http::request<http::string_body> blocked_req{http::verb::get, "/lmstat02", 11};
    const auto blocked_res = handleShortenerRequest(blocked_req, config, false);
    BOOST_TEST(blocked_res.result_int() == 410);

    const auto ok = linkRepository().getBySlug("lmstat01");
    const auto blocked = linkRepository().getBySlug("lmstat02");
    BOOST_REQUIRE(ok.has_value());
    BOOST_REQUIRE(blocked.has_value());
    BOOST_TEST(ok->stats.total_redirects == 1U);
    BOOST_TEST(blocked->stats.total_redirects == 0U);
}
