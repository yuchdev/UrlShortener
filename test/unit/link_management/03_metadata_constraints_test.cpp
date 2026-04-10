/** @file 03_metadata_constraints_test.cpp @brief Unit test: metadata validation enforces key/value and size constraints. */
#define BOOST_TEST_MODULE LinkManagementMetadataValidationTest
#include <boost/test/unit_test.hpp>
#include <unordered_map>
#include "../../../src/url_shortener.cpp"

/**
 * [Unit][Link Management] Metadata key/value limits are enforced.
 *
 * Scenario:
 *   Given valid metadata, then overly long key and value examples.
 *   When validateMetadata() runs.
 *   Then valid metadata passes and violating entries fail.
 *
 * API/Feature covered:
 *   - Stage 2 metadata size constraints.
 *
 * If this breaks:
 *   - Oversized metadata may bypass validation and break consumers.
 *   - First check key/value length checks and map limits.
 */
BOOST_AUTO_TEST_CASE(metadata_key_value_limits)
{
    std::unordered_map<std::string, std::string> ok{{"owner", "growth"}, {"locale", "en-US"}};
    BOOST_TEST(validateMetadata(ok));

    std::unordered_map<std::string, std::string> bad_key{{std::string(65, 'k'), "v"}};
    BOOST_TEST(!validateMetadata(bad_key));

    std::unordered_map<std::string, std::string> bad_value{{"k", std::string(513, 'v')}};
    BOOST_TEST(!validateMetadata(bad_value));
}
