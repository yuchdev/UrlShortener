/**
 * @file 01_link_command_service_create_happy_path.cpp
 * @brief Unit test: CreateLink with a valid https URL produces an active link
 * view.
 */
#define BOOST_TEST_MODULE LinkCommandServiceCreateHappyPath
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/LinkCommandService.hpp>

#include "AppTestFakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;
using namespace test_fakes;

/**
 * [Unit][App] CreateLink with a valid https URL succeeds.
 *
 * Scenario:
 *   Given an empty FakeLinkStore and a test ServerConfig.
 *   When CreateLink is called with "https://example.com" and no explicit slug.
 *   Then the result is ok(), the target_url matches, slug is non-empty,
 *   status is active, and short_url starts with the configured base domain.
 *
 * API/Feature covered:
 *   - app::LinkCommandService::CreateLink happy path.
 *   - URL acceptance, slug generation, view serialization field correctness.
 *
 * If this breaks, first check:
 *   - normalizeTargetUrl acceptance for public https:// URLs.
 *   - chooseSlug generation branch (no explicit slug supplied).
 *   - toView() status and short_url assembly.
 */
BOOST_AUTO_TEST_CASE(create_link_valid_https_url_returns_active_view)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    CreateLinkCommand cmd;
    cmd.target_url = "https://example.com/landing";

    const auto result = svc.CreateLink(cmd);

    BOOST_REQUIRE(result.ok());
    BOOST_REQUIRE(result.value.has_value());
    const auto& view = *result.value;

    BOOST_CHECK_EQUAL(view.target_url, "https://example.com/landing");
    BOOST_CHECK(!view.slug.empty());
    BOOST_CHECK(!view.id.empty());
    BOOST_CHECK_EQUAL(view.status, LinkStatus::active);
    BOOST_CHECK_EQUAL(view.enabled, true);

    // short_url must contain base domain + slug
    BOOST_CHECK(view.short_url.rfind("http://sho.rt/", 0) == 0);
    BOOST_CHECK(view.short_url.size() > std::string("http://sho.rt/").size());
}
