/**
 * @file 04_link_command_service_create_slug_conflict.cpp
 * @brief Unit test: CreateLink with an already-in-use explicit slug returns
 * slug_conflict.
 */
#define BOOST_TEST_MODULE LinkCommandServiceCreateSlugConflict
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_command_service.hpp>

#include "app_test_fakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;
using namespace test_fakes;

/**
 * [Unit][App] CreateLink with an existing slug returns slug_conflict.
 *
 * Scenario:
 *   Given FakeLinkStore already contains slug "taken".
 *   When CreateLink is called with slug = "taken".
 *   Then the result is !ok() with AppErrorCode::slug_conflict.
 *
 * API/Feature covered:
 *   - app::LinkCommandService::chooseSlug conflict detection path.
 *
 * If this breaks, first check:
 *   - ILinkStore::slugExists() delegation in chooseSlug().
 *   - Error code mapping for slug_conflict vs storage_failure.
 */
BOOST_AUTO_TEST_CASE(create_link_existing_explicit_slug_returns_slug_conflict)
{
    FakeLinkStore store;
    // Pre-seed "taken" so slugExists("taken") returns true.
    store.preseedSlugExists("taken");

    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    CreateLinkCommand cmd;
    cmd.target_url = "https://example.com";
    cmd.slug = std::string {"taken"};

    const auto result = svc.CreateLink(cmd);

    BOOST_CHECK(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::slug_conflict);
    BOOST_CHECK(!result.value.has_value());
}

/**
 * [Unit][App] CreateLink when store.create() reports slug_conflict error
 * propagates it.
 *
 * Scenario:
 *   Given FakeLinkStore configured to fail create() with slug_conflict
 *   (via an already-seeded link, not forceCreateStorageFailure).
 *   When CreateLink is called with the same explicit slug a second time.
 *   Then the result is !ok() with AppErrorCode::slug_conflict.
 */
BOOST_AUTO_TEST_CASE(create_link_store_create_returns_slug_conflict)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    // First creation – should succeed.
    CreateLinkCommand first_cmd;
    first_cmd.target_url = "https://example.com/one";
    first_cmd.slug = std::string {"myslug"};
    BOOST_REQUIRE(svc.CreateLink(first_cmd).ok());

    // Second creation with same explicit slug – store detects duplicate.
    CreateLinkCommand second_cmd;
    second_cmd.target_url = "https://example.com/two";
    second_cmd.slug = std::string {"myslug"};

    const auto result = svc.CreateLink(second_cmd);

    BOOST_CHECK(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::slug_conflict);
}
