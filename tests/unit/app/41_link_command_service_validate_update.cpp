/**
 * @file 41_link_command_service_validate_update.cpp
 * @brief Unit tests: ValidateUpdateLink (dry-run update) runs the same PATCH
 * validators and existence check as UpdateLink and returns the same would-be
 * view and error vocabulary, but never writes the record back.
 */
#define BOOST_TEST_MODULE LinkCommandServiceValidateUpdate
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/core/utils.hpp>

#include "app_test_fakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;
using namespace test_fakes;

namespace
{
/// Build and seed a link into the store, returning the seeded copy.
Link seedLink(FakeLinkStore& store, const std::string& slug)
{
    Link link;
    link.id = "id-" + slug;
    link.slug = slug;
    link.target_url = "https://target.example.com/path";
    link.created_at = currentTimestamp();
    link.updated_at = link.created_at;
    link.enabled = true;
    link.expires_at = "2999-01-01T00:00:00Z";
    link.tags = {"old"};
    link.metadata = {{"old", "value"}};
    link.redirect_type = RedirectType::temporary;
    store.seed(link);
    return link;
}
}  // namespace

/**
 * [Unit][App] Updating a missing slug via dry-run returns not_found, matching
 * UpdateLink.
 *
 * If this breaks, first check:
 *   - UpdateLinkImpl performs the findBySlug existence check for both variants.
 */
BOOST_AUTO_TEST_CASE(validate_update_missing_slug_returns_not_found)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    UpdateLinkCommand cmd;
    cmd.slug = "ghost";
    cmd.enabled = false;
    const auto result = svc.ValidateUpdateLink(cmd);
    BOOST_REQUIRE(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::not_found);
}

/**
 * [Unit][App] For the PATCH tri-state (absent / present value / explicit null),
 * the would-be dry-run view matches what UpdateLink would produce, while the
 * stored record is left byte-identical.
 */
BOOST_AUTO_TEST_CASE(validate_update_tristate_matches_real_and_store_unchanged)
{
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();

    const auto run_both = [&](const UpdateLinkCommand& cmd) {
        FakeLinkStore dry_store;
        FakeLinkStore real_store;
        seedLink(dry_store, cmd.slug);
        seedLink(real_store, cmd.slug);
        const Link before = *dry_store.findBySlug(cmd.slug);
        LinkCommandService dry_svc(dry_store, stats, config);
        LinkCommandService real_svc(real_store, stats, config);

        const auto dry = dry_svc.ValidateUpdateLink(cmd);
        const auto real = real_svc.UpdateLink(cmd);
        BOOST_REQUIRE(dry.ok());
        BOOST_REQUIRE(real.ok());

        // Would-be view mirrors the real update (ignoring updated_at, which is
        // a per-call timestamp).
        BOOST_CHECK_EQUAL(dry.value->enabled, real.value->enabled);
        BOOST_CHECK(dry.value->expires_at == real.value->expires_at);
        BOOST_CHECK(dry.value->tags == real.value->tags);
        BOOST_CHECK(dry.value->metadata == real.value->metadata);

        // Dry-run store record is byte-identical to the pre-call snapshot.
        const Link after = *dry_store.findBySlug(cmd.slug);
        BOOST_CHECK_EQUAL(after.enabled, before.enabled);
        BOOST_CHECK(after.expires_at == before.expires_at);
        BOOST_CHECK(after.tags == before.tags);
        BOOST_CHECK(after.metadata == before.metadata);
        BOOST_CHECK_EQUAL(after.updated_at, before.updated_at);
    };

    // Absent field (only enabled set), present value, and explicit-null expiry.
    UpdateLinkCommand present;
    present.slug = "s1";
    present.enabled = false;
    run_both(present);

    UpdateLinkCommand set_value;
    set_value.slug = "s2";
    set_value.expires_at = std::optional<std::string>("2030-06-01T12:00:00Z");
    run_both(set_value);

    UpdateLinkCommand explicit_null;
    explicit_null.slug = "s3";
    explicit_null.expires_at.emplace();  // outer present, inner empty => null.
    run_both(explicit_null);
}

/**
 * [Unit][App] An invalid present field returns the same invalid_field code via
 * dry-run as via UpdateLink, and leaves the store unchanged.
 */
BOOST_AUTO_TEST_CASE(validate_update_invalid_field_matches_real)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    const Link before = seedLink(store, "bad");
    LinkCommandService svc(store, stats, config);

    UpdateLinkCommand cmd;
    cmd.slug = "bad";
    cmd.expires_at = std::optional<std::string>("not-a-timestamp");

    const auto result = svc.ValidateUpdateLink(cmd);
    BOOST_REQUIRE(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::invalid_field);

    const Link after = *store.findBySlug("bad");
    BOOST_CHECK(after.expires_at == before.expires_at);
    BOOST_CHECK_EQUAL(after.updated_at, before.updated_at);
}

/**
 * [Unit][App] A forced store update failure does not surface under a dry run,
 * because no write is attempted; the stored record is unchanged.
 */
BOOST_AUTO_TEST_CASE(validate_update_ignores_forced_storage_failure)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    const Link before = seedLink(store, "boom");
    store.forceUpdateStorageFailure();
    LinkCommandService svc(store, stats, config);

    UpdateLinkCommand cmd;
    cmd.slug = "boom";
    cmd.enabled = false;

    const auto result = svc.ValidateUpdateLink(cmd);
    BOOST_REQUIRE(result.ok());
    BOOST_CHECK_EQUAL(result.value->enabled, false);

    // The store write was never attempted, so the record is unchanged.
    const Link after = *store.findBySlug("boom");
    BOOST_CHECK_EQUAL(after.enabled, before.enabled);
    BOOST_CHECK_EQUAL(after.updated_at, before.updated_at);
}
