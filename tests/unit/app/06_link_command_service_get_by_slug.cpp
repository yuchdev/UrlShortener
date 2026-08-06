/**
 * @file 06_link_command_service_get_by_slug.cpp
 * @brief Unit test: GetLink by slug returns the correct view when the slug
 * exists.
 */
#define BOOST_TEST_MODULE LinkCommandServiceGetBySlug
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/LinkCommandService.hpp>
#include <url_shortener/core/utils.h>

#include "AppTestFakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;
using namespace test_fakes;

/**
 * [Unit][App] GetLink by slug returns a populated view when slug is present.
 *
 * Scenario:
 *   Given FakeLinkStore with a seeded Link at slug "find-me".
 *   When GetLink is called with {by: slug, value: "find-me"}.
 *   Then the result is ok(), slug matches, target_url matches, id matches.
 *
 * API/Feature covered:
 *   - app::LinkCommandService::GetLink slug lookup path.
 *
 * If this breaks, first check:
 *   - ILinkStore::findBySlug delegation in GetLink.
 *   - toView() mapping from Link to LinkView.
 */
BOOST_AUTO_TEST_CASE(get_link_by_slug_returns_correct_view)
{
    FakeLinkStore store;

    Link link;
    link.id = "abc-id-001";
    link.slug = "find-me";
    link.target_url = "https://target.example.com/path";
    link.created_at = currentTimestamp();
    link.updated_at = link.created_at;
    link.enabled = true;
    link.redirect_type = RedirectType::temporary;
    store.seed(link);

    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    GetLinkQuery query;
    query.by = GetLinkBy::slug;
    query.value = "find-me";

    const auto result = svc.GetLink(query);

    BOOST_REQUIRE(result.ok());
    BOOST_REQUIRE(result.value.has_value());
    const auto& view = *result.value;

    BOOST_CHECK_EQUAL(view.id, "abc-id-001");
    BOOST_CHECK_EQUAL(view.slug, "find-me");
    BOOST_CHECK_EQUAL(view.target_url, "https://target.example.com/path");
    BOOST_CHECK_EQUAL(view.status, LinkStatus::active);
    BOOST_CHECK(view.short_url.rfind("http://sho.rt/find-me", 0) == 0);
}
