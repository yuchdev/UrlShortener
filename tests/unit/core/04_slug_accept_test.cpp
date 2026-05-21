/** @file 04_slug_accept_test.cpp @brief Unit test: slug validator accepts boundary valid values. */
#define BOOST_TEST_MODULE SlugAcceptTest
#include <boost/test/unit_test.hpp>
#include "../../../src/url_shortener.cpp"

/**
 * [Unit][Core] Slug validation accepts boundary-valid values.
 *
 * Scenario:
 *   Given a minimum valid slug and a maximum-length valid slug.
 *   When isValidSlug() evaluates both.
 *   Then both are accepted.
 *
 * API/Feature covered:
 *   - Slug validator acceptance boundaries.
 *
 * If this breaks:
 *   - Legitimate custom slugs may be rejected by the API.
 *   - First check length boundaries and allowed character pattern.
 */
BOOST_AUTO_TEST_CASE(slug_validator_accepts_boundary_values)
{
    BOOST_TEST(isValidSlug("Ab1"));
    BOOST_TEST(isValidSlug("a" + std::string(63, 'b')));
}
