/**
 * @file 09_stage2_management_unit_test.cpp
 * @brief Stage 2 unit coverage for lifecycle/state/validation helpers.
 */
#define BOOST_TEST_MODULE Stage2ManagementUnitTest
#include <boost/test/unit_test.hpp>

#include <string>
#include <unordered_map>
#include <vector>

#include "../../src/url_shortener.cpp"

BOOST_AUTO_TEST_CASE(status_precedence_deleted_over_disabled_over_expired)
{
    Link link;
    link.enabled = true;
    link.expires_at = std::string{"2099-01-01T00:00:00Z"};
    BOOST_TEST(resolveLinkStatus(link) == LinkStatus::active);

    link.expires_at = std::string{"2000-01-01T00:00:00Z"};
    BOOST_TEST(resolveLinkStatus(link) == LinkStatus::expired);

    link.enabled = false;
    BOOST_TEST(resolveLinkStatus(link) == LinkStatus::disabled);

    link.deleted_at = std::string{"2026-01-01T00:00:00Z"};
    BOOST_TEST(resolveLinkStatus(link) == LinkStatus::deleted);
}

BOOST_AUTO_TEST_CASE(tag_validation_constraints_and_dedupe)
{
    std::vector<std::string> tags{" alpha ", "beta", "beta"};
    BOOST_TEST(validateTags(tags));
    BOOST_REQUIRE_EQUAL(tags.size(), 2U);
    BOOST_TEST(tags[0] == "alpha");
    BOOST_TEST(tags[1] == "beta");

    std::vector<std::string> invalid{"bad tag"};
    BOOST_TEST(!validateTags(invalid));

    std::vector<std::string> too_many(21, "t");
    BOOST_TEST(!validateTags(too_many));
}

BOOST_AUTO_TEST_CASE(metadata_campaign_and_reserved_slug_validation)
{
    std::unordered_map<std::string, std::string> metadata{{"owner", "growth"}, {"locale", "en-US"}};
    BOOST_TEST(validateMetadata(metadata));

    std::unordered_map<std::string, std::string> invalid_key{{std::string(65, 'k'), "value"}};
    BOOST_TEST(!validateMetadata(invalid_key));

    Link::Campaign campaign;
    campaign.name = std::string{"spring-launch"};
    BOOST_TEST(validateCampaign(campaign));
    campaign.name = std::string(129, 'x');
    BOOST_TEST(!validateCampaign(campaign));

    BOOST_TEST(isReservedSlug("API"));
    BOOST_TEST(isReservedSlug("Stats"));
    BOOST_TEST(!isReservedSlug("promo2026"));
}

BOOST_AUTO_TEST_CASE(default_hooks_pass_through)
{
    Link link;
    http::request<http::string_body> req{http::verb::get, "/abc", 11};
    BOOST_TEST(passwordGuardCheck(link, req));
    BOOST_TEST(rateLimitGuardAllow(link, req));
}

BOOST_AUTO_TEST_CASE(stats_increment_only_on_successful_redirect)
{
    ServerConfig config;
    config.shortener_allow_private_targets = true;

    Link active;
    active.id = generateId();
    active.slug = "stg2active";
    active.target_url = "https://example.com/a";
    active.created_at = currentTimestamp();
    active.updated_at = active.created_at;
    BOOST_REQUIRE(linkRepository().create(active));

    Link disabled;
    disabled.id = generateId();
    disabled.slug = "stg2disab";
    disabled.target_url = "https://example.com/d";
    disabled.created_at = currentTimestamp();
    disabled.updated_at = disabled.created_at;
    disabled.enabled = false;
    BOOST_REQUIRE(linkRepository().create(disabled));

    http::request<http::string_body> active_req{http::verb::get, "/stg2active", 11};
    const auto active_res = handleShortenerRequest(active_req, config, false);
    BOOST_TEST(active_res.result_int() == 302);

    http::request<http::string_body> disabled_req{http::verb::get, "/stg2disab", 11};
    const auto disabled_res = handleShortenerRequest(disabled_req, config, false);
    BOOST_TEST(disabled_res.result_int() == 410);

    const auto active_after = linkRepository().getBySlug("stg2active");
    BOOST_REQUIRE(active_after.has_value());
    BOOST_TEST(active_after->stats.total_redirects == 1U);
    BOOST_TEST(active_after->stats.last_accessed_at.has_value());

    const auto disabled_after = linkRepository().getBySlug("stg2disab");
    BOOST_REQUIRE(disabled_after.has_value());
    BOOST_TEST(disabled_after->stats.total_redirects == 0U);
    BOOST_TEST(!disabled_after->stats.last_accessed_at.has_value());
}
