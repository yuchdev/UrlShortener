/** @file 01_url_validation_accept_test.cpp @brief Unit test: valid http/https absolute URLs are accepted. */
#define BOOST_TEST_MODULE UrlValidationAcceptTest
#include <boost/test/unit_test.hpp>
#include "../../../src/url_shortener.cpp"

/**
 * [Unit][Core] URL validation accepts safe absolute HTTP(S) targets.
 *
 * Scenario:
 *   Given the URL normalization pipeline with private targets allowed.
 *   When absolute http:// and https:// URLs are submitted.
 *   Then normalization/validation accepts both URLs.
 *
 * API/Feature covered:
 *   - normalizeTargetUrl() acceptance path for safe schemes.
 *
 * If this breaks:
 *   - Valid customer destinations may be rejected at create-time.
 *   - First check scheme allowlist and absolute URL validation logic.
 */
BOOST_AUTO_TEST_CASE(url_validation_accepts_valid_http_https_absolute_urls)
{
    ServerConfig cfg;
    cfg.shortener_allow_private_targets = true;
    BOOST_TEST(normalizeTargetUrl("http://example.com/a", cfg).has_value());
    BOOST_TEST(normalizeTargetUrl("https://example.com/b", cfg).has_value());
}
