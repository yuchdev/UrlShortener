/** @file 02_tag_validation_test.cpp @brief Unit test: tag validation enforces bounds, regex, trimming and dedupe. */
#define BOOST_TEST_MODULE LinkManagementTagValidationTest
#include <boost/test/unit_test.hpp>
#include <vector>
#include "../../../src/url_shortener.cpp"

/**
 * [Unit][Link Management] Tag validation enforces normalization and constraints.
 *
 * Scenario:
 *   Given tags with whitespace, duplicates, invalid chars, and too many entries.
 *   When validateTags() is called.
 *   Then values are trimmed/deduped and invalid sets are rejected.
 *
 * API/Feature covered:
 *   - Stage 2 tags validation contract.
 *
 * If this breaks:
 *   - Tag data shape can violate API guarantees.
 *   - First check trim/dedupe path, regex checks, and max count.
 */
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
