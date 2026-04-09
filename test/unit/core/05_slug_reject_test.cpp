/** @file 05_slug_reject_test.cpp @brief Unit test: slug validator rejects invalid chars/length. */
#define BOOST_TEST_MODULE SlugRejectTest
#include <boost/test/unit_test.hpp>
#include "../../../src/url_shortener.cpp"

BOOST_AUTO_TEST_CASE(slug_validator_rejects_invalid_chars_and_length)
{
    BOOST_TEST(!isValidSlug("ab"));
    BOOST_TEST(!isValidSlug("_abc"));
    BOOST_TEST(!isValidSlug("ab!c"));
    BOOST_TEST(!isValidSlug(std::string(65, 'a')));
}
