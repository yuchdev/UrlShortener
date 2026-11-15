/**
 * @file 40_link_command_service_validate_create.cpp
 * @brief Unit tests: ValidateCreateLink (dry-run create) runs the same
 * validators as CreateLink and returns the same error vocabulary, but never
 * mutates the store, never evicts the cache, and never reserves a generated
 * slug.
 */
#define BOOST_TEST_MODULE LinkCommandServiceValidateCreate
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_command_service.hpp>

#include "app_test_fakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;
using namespace test_fakes;

/**
 * [Unit][App] A valid dry-run create returns the same view CreateLink would
 * (ignoring id/timestamps) and leaves the store empty.
 *
 * Scenario:
 *   Given two fresh FakeLinkStores and the same valid create command (explicit
 *   slug so the slug is deterministic).
 *   When ValidateCreateLink runs against the first store and CreateLink against
 *   the second.
 *   Then the would-be view equals the created view on every field except id and
 *   timestamps, and the dry-run store still returns nullopt for the slug.
 *
 * If this breaks, first check:
 *   - CreateLinkImpl shares one validate-and-build path for both variants.
 *   - The persist=false branch skips store_.create / invalidateCache.
 */
BOOST_AUTO_TEST_CASE(validate_create_matches_real_view_and_leaves_store_empty)
{
    FakeLinkStore dry_store;
    FakeLinkStore real_store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService dry_svc(dry_store, stats, config);
    LinkCommandService real_svc(real_store, stats, config);

    CreateLinkCommand cmd;
    cmd.target_url = "https://example.com/landing";
    cmd.slug = std::string {"deterministic"};
    cmd.tags = {"alpha", "beta"};
    cmd.metadata = {{"k", "v"}};

    const auto dry = dry_svc.ValidateCreateLink(cmd);
    const auto real = real_svc.CreateLink(cmd);

    BOOST_REQUIRE(dry.ok());
    BOOST_REQUIRE(real.ok());
    const auto& dv = *dry.value;
    const auto& rv = *real.value;

    // Same observable view (id and timestamps are freshly generated per call).
    BOOST_CHECK_EQUAL(dv.slug, rv.slug);
    BOOST_CHECK_EQUAL(dv.target_url, rv.target_url);
    BOOST_CHECK_EQUAL(dv.short_url, rv.short_url);
    BOOST_CHECK_EQUAL(dv.enabled, rv.enabled);
    BOOST_CHECK_EQUAL(dv.status, rv.status);
    BOOST_CHECK(dv.tags == rv.tags);
    BOOST_CHECK(dv.metadata == rv.metadata);
    BOOST_CHECK(!dv.id.empty());

    // Dry-run store is unchanged; the real store holds the record.
    BOOST_CHECK(!dry_store.findBySlug("deterministic").has_value());
    BOOST_CHECK(!dry_store.slugExists("deterministic"));
    BOOST_CHECK(real_store.findBySlug("deterministic").has_value());
}

/**
 * [Unit][App] A dry-run create with a generated slug does not reserve it: the
 * store stays empty and a later real create of the same command succeeds.
 */
BOOST_AUTO_TEST_CASE(validate_create_generated_slug_not_reserved)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    CreateLinkCommand cmd;
    cmd.target_url = "https://example.com/generated";

    const auto dry = svc.ValidateCreateLink(cmd);
    BOOST_REQUIRE(dry.ok());
    const auto generated_slug = dry.value->slug;
    BOOST_CHECK(!generated_slug.empty());

    // The generated slug was not consumed or reserved.
    BOOST_CHECK(!store.findBySlug(generated_slug).has_value());
    BOOST_CHECK(!store.slugExists(generated_slug));

    // A subsequent real create still succeeds (slug space untouched).
    const auto real = svc.CreateLink(cmd);
    BOOST_REQUIRE(real.ok());
    BOOST_CHECK(store.findBySlug(real.value->slug).has_value());
}

/**
 * [Unit][App] Each invalid-input class returns the same AppErrorCode from
 * ValidateCreateLink as from CreateLink.
 *
 * If this breaks, first check:
 *   - The validators run identically regardless of the persist flag.
 */
BOOST_AUTO_TEST_CASE(validate_create_error_codes_match_real_create)
{
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();

    const auto run_both = [&](const CreateLinkCommand& cmd,
                              bool conflict_preseed) {
        FakeLinkStore dry_store;
        FakeLinkStore real_store;
        if (conflict_preseed) {
            dry_store.preseedSlugExists("taken");
            real_store.preseedSlugExists("taken");
        }
        LinkCommandService dry_svc(dry_store, stats, config);
        LinkCommandService real_svc(real_store, stats, config);
        const auto dry = dry_svc.ValidateCreateLink(cmd);
        const auto real = real_svc.CreateLink(cmd);
        BOOST_REQUIRE(!dry.ok());
        BOOST_REQUIRE(!real.ok());
        BOOST_CHECK_EQUAL(dry.error.code, real.error.code);
    };

    // Invalid URL.
    CreateLinkCommand bad_url;
    bad_url.target_url = "not-a-url";
    run_both(bad_url, false);

    // Reserved slug.
    CreateLinkCommand reserved;
    reserved.target_url = "https://example.com";
    reserved.slug = std::string {"health"};
    run_both(reserved, false);

    // Invalid slug format.
    CreateLinkCommand bad_slug;
    bad_slug.target_url = "https://example.com";
    bad_slug.slug = std::string {"has spaces!"};
    run_both(bad_slug, false);

    // Slug conflict.
    CreateLinkCommand conflict;
    conflict.target_url = "https://example.com";
    conflict.slug = std::string {"taken"};
    run_both(conflict, true);

    // Bad expires_at.
    CreateLinkCommand bad_expiry;
    bad_expiry.target_url = "https://example.com";
    bad_expiry.expires_at = std::string {"not-a-timestamp"};
    run_both(bad_expiry, false);

    // Tag violation (empty tag).
    CreateLinkCommand bad_tags;
    bad_tags.target_url = "https://example.com";
    bad_tags.tags = {""};
    run_both(bad_tags, false);

    // Metadata violation (oversized value).
    CreateLinkCommand bad_meta;
    bad_meta.target_url = "https://example.com";
    bad_meta.metadata = {{"k", std::string(513, 'x')}};
    run_both(bad_meta, false);

    // Campaign violation (oversized field).
    Link::Campaign oversized;
    oversized.name = std::string(129, 'y');
    CreateLinkCommand bad_campaign;
    bad_campaign.target_url = "https://example.com";
    bad_campaign.campaign = oversized;
    run_both(bad_campaign, false);
}

/**
 * [Unit][App] A forced store create failure does not surface under a dry run,
 * because no write is attempted; the store stays empty.
 */
BOOST_AUTO_TEST_CASE(validate_create_ignores_forced_storage_failure)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    store.forceCreateStorageFailure();
    LinkCommandService svc(store, stats, config);

    CreateLinkCommand cmd;
    cmd.target_url = "https://example.com";
    cmd.slug = std::string {"nowrite"};

    const auto result = svc.ValidateCreateLink(cmd);
    BOOST_REQUIRE(result.ok());
    BOOST_CHECK(!store.findBySlug("nowrite").has_value());
}
