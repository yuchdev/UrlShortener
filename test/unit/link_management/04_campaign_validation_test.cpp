/** @file 04_campaign_validation_test.cpp @brief Unit test: campaign fields obey per-field length constraints. */
#define BOOST_TEST_MODULE LinkManagementCampaignValidationTest
#include <boost/test/unit_test.hpp>
#include "../../../src/url_shortener.cpp"

/**
 * [Unit][Link Management] Campaign field length validation works.
 *
 * Scenario:
 *   Given a valid campaign and then an overlong medium value.
 *   When validateCampaign() evaluates the payload.
 *   Then valid campaign passes and overlong field is rejected.
 *
 * API/Feature covered:
 *   - Stage 2 campaign field constraints.
 *
 * If this breaks:
 *   - Campaign metadata contract can drift from specification.
 *   - First check per-field max length enforcement.
 */
BOOST_AUTO_TEST_CASE(campaign_field_limits)
{
    Link::Campaign campaign;
    campaign.name = std::string{"spring-launch"};
    campaign.source = std::string{"newsletter"};
    BOOST_TEST(validateCampaign(campaign));

    campaign.medium = std::string(129, 'm');
    BOOST_TEST(!validateCampaign(campaign));
}
