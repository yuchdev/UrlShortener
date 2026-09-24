/**
 * @file 18_link_command_service_update_storage_failure.cpp
 * @brief Unit tests: when the underlying store fails to persist a write, the
 * update/delete/enable/restore commands surface storage_failure (not silent
 * success). Guards the ILinkStore::update void->Result contract.
 */
#define BOOST_TEST_MODULE LinkCommandServiceUpdateStorageFailure
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/core/utils.h>

#include "app_test_fakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;
using namespace test_fakes;

namespace
{
void seedActiveLink(FakeLinkStore& store, const std::string& slug)
{
    Link link;
    link.id = "id-" + slug;
    link.slug = slug;
    link.target_url = "https://target.example.com/path";
    link.created_at = currentTimestamp();
    link.updated_at = link.created_at;
    link.enabled = true;
    link.redirect_type = RedirectType::temporary;
    store.seed(link);
}
}  // namespace

/**
 * [Unit][App] UpdateLink maps a store persistence failure to storage_failure.
 *
 * If this breaks, first check:
 *   - LinkCommandService::UpdateLink checks the bool result of store.update.
 *   - ILinkStore::update returns false (with AppError) on a persistence failure.
 */
BOOST_AUTO_TEST_CASE(update_store_failure_returns_storage_failure)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    seedActiveLink(store, "boom");
    store.forceUpdateStorageFailure();
    LinkCommandService svc(store, stats, config);

    UpdateLinkCommand cmd;
    cmd.slug = "boom";
    cmd.enabled = false;
    const auto result = svc.UpdateLink(cmd);

    BOOST_REQUIRE(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::storage_failure);
}

/**
 * [Unit][App] DeleteLink maps a store persistence failure to storage_failure.
 */
BOOST_AUTO_TEST_CASE(delete_store_failure_returns_storage_failure)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    seedActiveLink(store, "boom");
    store.forceUpdateStorageFailure();
    LinkCommandService svc(store, stats, config);

    DeleteLinkCommand cmd;
    cmd.slug = "boom";
    const auto result = svc.DeleteLink(cmd);

    BOOST_REQUIRE(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::storage_failure);
}

/**
 * [Unit][App] SetLinkEnabled maps a store persistence failure to
 * storage_failure.
 */
BOOST_AUTO_TEST_CASE(set_enabled_store_failure_returns_storage_failure)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    seedActiveLink(store, "boom");
    store.forceUpdateStorageFailure();
    LinkCommandService svc(store, stats, config);

    SetLinkEnabledCommand cmd;
    cmd.slug = "boom";
    cmd.enabled = false;
    const auto result = svc.SetLinkEnabled(cmd);

    BOOST_REQUIRE(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::storage_failure);
}

/**
 * [Unit][App] RestoreLink maps a store persistence failure to storage_failure.
 */
BOOST_AUTO_TEST_CASE(restore_store_failure_returns_storage_failure)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    seedActiveLink(store, "boom");
    store.forceUpdateStorageFailure();
    LinkCommandService svc(store, stats, config);

    RestoreLinkCommand cmd;
    cmd.slug = "boom";
    const auto result = svc.RestoreLink(cmd);

    BOOST_REQUIRE(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::storage_failure);
}
