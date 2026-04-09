/** @file 04_slug_accept_test.cpp @brief Unit test: slug validator accepts boundary valid values. */
#define BOOST_TEST_MODULE SlugAcceptTest
#include <boost/test/unit_test.hpp>
#include "../../../src/url_shortener.cpp"

BOOST_AUTO_TEST_CASE(slug_validator_accepts_boundary_values)
{
    BOOST_TEST(isValidSlug("Ab1"));
    BOOST_TEST(isValidSlug("a" + std::string(63, 'b')));
}
