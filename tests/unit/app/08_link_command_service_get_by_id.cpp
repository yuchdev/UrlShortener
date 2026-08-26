/**
 * @file 08_link_command_service_get_by_id.cpp
 * @brief Unit test: GetLink by id returns the correct view when id is present.
 */
#define BOOST_TEST_MODULE LinkCommandServiceGetById
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/core/utils.h>

#include "app_test_fakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;
using namespace test_fakes;

/**
 * [Unit][App] GetLink by id returns the correct view.
 *
 * Scenario:
 *   Given FakeLinkStore with a seeded Link whose id is "fixed-id-007".
 *   When GetLink is called with {by: id, value: "fixed-id-007"}.
 *   Then result is ok(), view.id == "fixed-id-007", view.slug matches.
 *
 * API/Feature covered:
 *   - app::LinkCommandService::GetLink id lookup path.
 *
 * If this breaks, first check:
 *   - ILinkStore::findById delegation in GetLink.
 *   - toView() field mapping.
 */
BOOST_AUTO_TEST_CASE(get_link_by_id_returns_correct_view)
{
    FakeLinkStore store;

    Link link;
    link.id = "fixed-id-007";
    link.slug = "by-id-slug";
    link.target_url = "https://byid.example.com";
    link.created_at = currentTimestamp();
    link.updated_at = link.created_at;
    link.enabled = true;
    link.redirect_type = RedirectType::permanent;
    store.seed(link);

    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    GetLinkQuery query;
    query.by = GetLinkBy::id;
    query.value = "fixed-id-007";

    const auto result = svc.GetLink(query);

    BOOST_REQUIRE(result.ok());
    BOOST_REQUIRE(result.value.has_value());
    BOOST_CHECK_EQUAL(result.value->id, "fixed-id-007");
    BOOST_CHECK_EQUAL(result.value->slug, "by-id-slug");
    BOOST_CHECK_EQUAL(result.value->target_url, "https://byid.example.com");
    BOOST_CHECK_EQUAL(result.value->redirect_type, RedirectType::permanent);
}
