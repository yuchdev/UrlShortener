#define BOOST_TEST_MODULE LinkManagementCampaignValidationTest
#include <boost/test/unit_test.hpp>
#include "../../../src/url_shortener.cpp"

BOOST_AUTO_TEST_CASE(campaign_field_limits)
{
    Link::Campaign campaign;
    campaign.name = std::string{"spring-launch"};
    campaign.source = std::string{"newsletter"};
    BOOST_TEST(validateCampaign(campaign));

    campaign.medium = std::string(129, 'm');
    BOOST_TEST(!validateCampaign(campaign));
}
