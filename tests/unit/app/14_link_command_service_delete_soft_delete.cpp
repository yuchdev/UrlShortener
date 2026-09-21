/**
 * @file 14_link_command_service_delete_soft_delete.cpp
 * @brief Unit tests: DeleteLink soft-deletes by setting deleted_at and
 * propagates not_found.
 */
#define BOOST_TEST_MODULE LinkCommandServiceDelete
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
 * [Unit][App] DeleteLink soft-deletes: deleted_at is populated and the record
 * is written back (still findable), not physically removed.
 *
 * If this breaks, first check:
 *   - DeleteLink sets deleted_at via currentTimestamp() and calls store.update.
 */
BOOST_AUTO_TEST_CASE(delete_sets_deleted_at)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    seedActiveLink(store, "gone");
    LinkCommandService svc(store, stats, config);

    DeleteLinkCommand cmd;
    cmd.slug = "gone";
    const auto result = svc.DeleteLink(cmd);

    BOOST_REQUIRE(result.ok());
    BOOST_REQUIRE(result.value->deleted_at.has_value());
    BOOST_CHECK(!result.value->deleted_at->empty());

    // Soft delete keeps the record retrievable in the store.
    const auto persisted = store.findBySlug("gone");
    BOOST_REQUIRE(persisted.has_value());
    BOOST_CHECK(persisted->deleted_at.has_value());
}

/**
 * [Unit][App] Deleting a missing slug returns not_found.
 */
BOOST_AUTO_TEST_CASE(delete_missing_slug_returns_not_found)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    DeleteLinkCommand cmd;
    cmd.slug = "ghost";
    const auto result = svc.DeleteLink(cmd);

    BOOST_REQUIRE(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::not_found);
}
