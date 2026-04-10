/** @file 05_slug_reject_test.cpp @brief Unit test: slug validator rejects invalid chars/length. */
#define BOOST_TEST_MODULE SlugRejectTest
#include <boost/test/unit_test.hpp>
#include "../../../src/url_shortener.cpp"

/**
 * [Unit][Core] Slug validation rejects invalid patterns and lengths.
 *
 * Scenario:
 *   Given undersized, oversized, and invalid-character slug candidates.
 *   When isValidSlug() validates these inputs.
 *   Then each invalid slug is rejected.
 *
 * API/Feature covered:
 *   - Slug validator rejection rules.
 *
 * If this breaks:
 *   - Invalid route segments can enter persistence and routing.
 *   - First check regex/prefix constraints and min/max length checks.
 */
BOOST_AUTO_TEST_CASE(slug_validator_rejects_invalid_chars_and_length)
{
    BOOST_TEST(!isValidSlug("ab"));
    BOOST_TEST(!isValidSlug("_abc"));
    BOOST_TEST(!isValidSlug("ab!c"));
    BOOST_TEST(!isValidSlug(std::string(65, 'a')));
}
