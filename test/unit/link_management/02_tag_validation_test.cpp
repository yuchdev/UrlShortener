#define BOOST_TEST_MODULE LinkManagementTagValidationTest
#include <boost/test/unit_test.hpp>
#include <vector>
#include "../../../src/url_shortener.cpp"

BOOST_AUTO_TEST_CASE(tag_validation_bounds_regex_and_dedupe)
{
    std::vector<std::string> tags{" spring ", "paid", "paid"};
    BOOST_TEST(validateTags(tags));
    BOOST_REQUIRE_EQUAL(tags.size(), 2U);
    BOOST_TEST(tags[0] == "spring");
    BOOST_TEST(tags[1] == "paid");

    std::vector<std::string> invalid_chars{"bad tag"};
    BOOST_TEST(!validateTags(invalid_chars));

    std::vector<std::string> too_many(21, "x");
    BOOST_TEST(!validateTags(too_many));
}
