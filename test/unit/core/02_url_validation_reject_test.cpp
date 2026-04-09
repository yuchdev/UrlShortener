/** @file 02_url_validation_reject_test.cpp @brief Unit test: invalid/unsafe schemes are rejected. */
#define BOOST_TEST_MODULE UrlValidationRejectTest
#include <boost/test/unit_test.hpp>
#include "../../../src/url_shortener.cpp"

BOOST_AUTO_TEST_CASE(url_validation_rejects_invalid_unsafe_schemes)
{
    ServerConfig cfg;
    BOOST_TEST(!normalizeTargetUrl("ftp://example.com", cfg).has_value());
    BOOST_TEST(!normalizeTargetUrl("javascript:alert(1)", cfg).has_value());
    BOOST_TEST(!normalizeTargetUrl("/relative/path", cfg).has_value());
    BOOST_TEST(!normalizeTargetUrl("https://user:pass@example.com", cfg).has_value());
}
