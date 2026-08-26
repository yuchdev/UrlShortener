/**
 * @file 07_link_command_service_get_not_found.cpp
 * @brief Unit test: GetLink returns not_found when slug or id is absent.
 */
#define BOOST_TEST_MODULE LinkCommandServiceGetNotFound
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_command_service.hpp>

#include "app_test_fakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;
using namespace test_fakes;

/**
 * [Unit][App] GetLink by slug with unknown slug returns not_found.
 *
 * Scenario:
 *   Given an empty FakeLinkStore.
 *   When GetLink is called with {by: slug, value: "doesntexist"}.
 *   Then the result is !ok() with AppErrorCode::not_found.
 *
 * API/Feature covered:
 *   - app::LinkCommandService::GetLink not-found path for slug lookup.
 *
 * If this breaks, first check:
 *   - GetLink error mapping when findBySlug returns nullopt.
 */
BOOST_AUTO_TEST_CASE(get_link_by_unknown_slug_returns_not_found)
{
    FakeLinkStore store;  // empty
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    GetLinkQuery query;
    query.by = GetLinkBy::slug;
    query.value = "doesntexist";

    const auto result = svc.GetLink(query);

    BOOST_CHECK(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::not_found);
    BOOST_CHECK(!result.value.has_value());
}

/**
 * [Unit][App] GetLink by id with unknown id returns not_found.
 */
BOOST_AUTO_TEST_CASE(get_link_by_unknown_id_returns_not_found)
{
    FakeLinkStore store;  // empty
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    GetLinkQuery query;
    query.by = GetLinkBy::id;
    query.value = "no-such-id";

    const auto result = svc.GetLink(query);

    BOOST_CHECK(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::not_found);
    BOOST_CHECK(!result.value.has_value());
}
