#define BOOST_TEST_MODULE LinkManagementMetadataValidationTest
#include <boost/test/unit_test.hpp>
#include <unordered_map>
#include "../../../src/url_shortener.cpp"

BOOST_AUTO_TEST_CASE(metadata_key_value_limits)
{
    std::unordered_map<std::string, std::string> ok{{"owner", "growth"}, {"locale", "en-US"}};
    BOOST_TEST(validateMetadata(ok));

    std::unordered_map<std::string, std::string> bad_key{{std::string(65, 'k'), "v"}};
    BOOST_TEST(!validateMetadata(bad_key));

    std::unordered_map<std::string, std::string> bad_value{{"k", std::string(513, 'v')}};
    BOOST_TEST(!validateMetadata(bad_value));
}
