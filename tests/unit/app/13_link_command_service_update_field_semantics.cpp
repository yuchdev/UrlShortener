/**
 * @file 13_link_command_service_update_field_semantics.cpp
 * @brief Unit tests: UpdateLink honors PATCH present/absent/null semantics and
 * reuses the shared tag/metadata/campaign validators.
 */
#define BOOST_TEST_MODULE LinkCommandServiceUpdate
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/core/utils.h>

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

LinkCommandService makeService(FakeLinkStore& store,
                               FakeLinkStatsReader& stats,
                               const ServerConfig& config)
{
    return LinkCommandService(store, stats, config);
}
}  // namespace

/**
 * [Unit][App] An absent field leaves the stored value untouched; a present
 * field replaces it.
 *
 * Scenario:
 *   Given a seeded link with enabled=true and one tag.
 *   When UpdateLink is called with only enabled=false set.
 *   Then enabled flips to false and tags remain unchanged.
 *
 * If this breaks, first check:
 *   - UpdateLink's has_value() gating per field.
 */
BOOST_AUTO_TEST_CASE(update_present_field_replaces_absent_field_untouched)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    seedLink(store, "keep");
    auto svc = makeService(store, stats, config);

    UpdateLinkCommand cmd;
    cmd.slug = "keep";
    cmd.enabled = false;

    const auto result = svc.UpdateLink(cmd);
    BOOST_REQUIRE(result.ok());
    BOOST_REQUIRE(result.value.has_value());
    BOOST_CHECK_EQUAL(result.value->enabled, false);
    BOOST_REQUIRE_EQUAL(result.value->tags.size(), 1u);
    BOOST_CHECK_EQUAL(result.value->tags[0], "old");
}

/**
 * [Unit][App] A present expires_at with a valid RFC3339 value replaces it; an
 * explicit null clears it; an absent field leaves it untouched.
 */
BOOST_AUTO_TEST_CASE(update_expires_at_set_null_and_absent)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    seedLink(store, "exp");
    auto svc = makeService(store, stats, config);

    // Set to a new valid value.
    UpdateLinkCommand set_cmd;
    set_cmd.slug = "exp";
    set_cmd.expires_at = std::optional<std::string>("2030-06-01T12:00:00Z");
    const auto set_res = svc.UpdateLink(set_cmd);
    BOOST_REQUIRE(set_res.ok());
    BOOST_REQUIRE(set_res.value->expires_at.has_value());
    BOOST_CHECK_EQUAL(*set_res.value->expires_at, "2030-06-01T12:00:00Z");

    // Explicit null (outer present, inner empty) clears it.
    UpdateLinkCommand null_cmd;
    null_cmd.slug = "exp";
    null_cmd.expires_at.emplace();
    const auto null_res = svc.UpdateLink(null_cmd);
    BOOST_REQUIRE(null_res.ok());
    BOOST_CHECK(!null_res.value->expires_at.has_value());

    // Absent leaves it (now cleared) untouched.
    UpdateLinkCommand absent_cmd;
    absent_cmd.slug = "exp";
    absent_cmd.enabled = true;
    const auto absent_res = svc.UpdateLink(absent_cmd);
    BOOST_REQUIRE(absent_res.ok());
    BOOST_CHECK(!absent_res.value->expires_at.has_value());
}

/**
 * [Unit][App] A malformed expires_at value is rejected with invalid_field.
 */
BOOST_AUTO_TEST_CASE(update_expires_at_invalid_returns_invalid_field)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    seedLink(store, "bad-exp");
    auto svc = makeService(store, stats, config);

    UpdateLinkCommand cmd;
    cmd.slug = "bad-exp";
    cmd.expires_at = std::optional<std::string>("not-a-timestamp");
    const auto result = svc.UpdateLink(cmd);
    BOOST_REQUIRE(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::invalid_field);
}

/**
 * [Unit][App] tags/metadata/campaign present with valid values are replaced.
 */
BOOST_AUTO_TEST_CASE(update_tags_metadata_campaign_valid_replace)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    seedLink(store, "collections");
    auto svc = makeService(store, stats, config);

    Link::Campaign campaign;
    campaign.name = "spring";

    UpdateLinkCommand cmd;
    cmd.slug = "collections";
    cmd.tags = std::vector<std::string>{"new-tag", "second"};
    cmd.metadata =
        std::unordered_map<std::string, std::string>{{"k", "v"}};
    cmd.campaign = std::optional<Link::Campaign>(campaign);

    const auto result = svc.UpdateLink(cmd);
    BOOST_REQUIRE(result.ok());
    BOOST_REQUIRE_EQUAL(result.value->tags.size(), 2u);
    BOOST_CHECK_EQUAL(result.value->metadata.at("k"), "v");
    BOOST_REQUIRE(result.value->campaign.has_value());
    BOOST_REQUIRE(result.value->campaign->name.has_value());
    BOOST_CHECK_EQUAL(*result.value->campaign->name, "spring");
}

/**
 * [Unit][App] An explicit-null campaign clears the stored campaign.
 */
BOOST_AUTO_TEST_CASE(update_campaign_null_clears)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    Link seeded = seedLink(store, "camp");
    Link::Campaign campaign;
    campaign.name = "existing";
    seeded.campaign = campaign;
    store.seed(seeded);
    auto svc = makeService(store, stats, config);

    UpdateLinkCommand cmd;
    cmd.slug = "camp";
    cmd.campaign.emplace();  // outer present, inner empty => explicit null.
    const auto result = svc.UpdateLink(cmd);
    BOOST_REQUIRE(result.ok());
    BOOST_CHECK(!result.value->campaign.has_value());
}

/**
 * [Unit][App] Invalid tags, metadata, and campaign each return invalid_field.
 */
BOOST_AUTO_TEST_CASE(update_invalid_collections_return_invalid_field)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    seedLink(store, "invalid");
    auto svc = makeService(store, stats, config);

    // Empty tag is rejected by validateTags.
    UpdateLinkCommand tag_cmd;
    tag_cmd.slug = "invalid";
    tag_cmd.tags = std::vector<std::string>{""};
    const auto tag_res = svc.UpdateLink(tag_cmd);
    BOOST_REQUIRE(!tag_res.ok());
    BOOST_CHECK_EQUAL(tag_res.error.code, AppErrorCode::invalid_field);

    // Oversized metadata value is rejected by validateMetadata.
    UpdateLinkCommand meta_cmd;
    meta_cmd.slug = "invalid";
    meta_cmd.metadata = std::unordered_map<std::string, std::string>{
        {"k", std::string(513, 'x')}};
    const auto meta_res = svc.UpdateLink(meta_cmd);
    BOOST_REQUIRE(!meta_res.ok());
    BOOST_CHECK_EQUAL(meta_res.error.code, AppErrorCode::invalid_field);

    // Oversized campaign field is rejected by validateCampaign.
    Link::Campaign campaign;
    campaign.name = std::string(129, 'y');
    UpdateLinkCommand camp_cmd;
    camp_cmd.slug = "invalid";
    camp_cmd.campaign = std::optional<Link::Campaign>(campaign);
    const auto camp_res = svc.UpdateLink(camp_cmd);
    BOOST_REQUIRE(!camp_res.ok());
    BOOST_CHECK_EQUAL(camp_res.error.code, AppErrorCode::invalid_field);
}

/**
 * [Unit][App] Updating a missing slug returns not_found.
 */
BOOST_AUTO_TEST_CASE(update_missing_slug_returns_not_found)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    auto svc = makeService(store, stats, config);

    UpdateLinkCommand cmd;
    cmd.slug = "ghost";
    cmd.enabled = false;
    const auto result = svc.UpdateLink(cmd);
    BOOST_REQUIRE(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::not_found);
}
