/** @file 02_url_validation_reject_test.cpp @brief Unit test: invalid/unsafe schemes are rejected. */
#define BOOST_TEST_MODULE UrlValidationRejectTest
#include <boost/test/unit_test.hpp>
#include "../../../src/url_shortener.cpp"

/**
 * [Unit][Core] URL validation rejects malformed and unsafe inputs.
 *
 * Scenario:
 *   Given disallowed schemes, relative paths, and credentialed URLs.
 *   When normalizeTargetUrl() validates the input.
 *   Then each unsafe/malformed value is rejected.
 *
 * API/Feature covered:
 *   - normalizeTargetUrl() security rejection rules.
 *
 * If this breaks:
 *   - Dangerous destinations may bypass validation.
 *   - First check scheme filtering and path/userinfo guards.
 */
BOOST_AUTO_TEST_CASE(url_validation_rejects_invalid_unsafe_schemes)
{
    ServerConfig cfg;
    BOOST_TEST(!normalizeTargetUrl("ftp://example.com", cfg).has_value());
    BOOST_TEST(!normalizeTargetUrl("javascript:alert(1)", cfg).has_value());
    BOOST_TEST(!normalizeTargetUrl("/relative/path", cfg).has_value());
    BOOST_TEST(!normalizeTargetUrl("https://user:pass@example.com", cfg).has_value());
}
