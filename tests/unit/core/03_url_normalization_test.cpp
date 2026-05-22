/** @file 03_url_normalization_test.cpp @brief Unit test: normalization lowercases and strips default ports. */
#define BOOST_TEST_MODULE UrlNormalizationTest
#include <boost/test/unit_test.hpp>
#include "../../../src/url_shortener.cpp"

/**
 * [Unit][Core] URL normalization canonicalizes case and default ports.
 *
 * Scenario:
 *   Given a URL with mixed case, whitespace, and default HTTPS port.
 *   When normalizeTargetUrl() processes it.
 *   Then scheme/host are lowercased and :443 is removed.
 *
 * API/Feature covered:
 *   - Canonicalization behavior in normalizeTargetUrl().
 *
 * If this breaks:
 *   - Equivalent URLs may no longer normalize consistently.
 *   - First check trimming, lowercasing, and default-port stripping.
 */
BOOST_AUTO_TEST_CASE(url_normalization_lowercases_and_strips_default_ports)
{
    ServerConfig cfg;
    cfg.shortener_allow_private_targets = true;
    const auto normalized = normalizeTargetUrl("  HTTPS://Example.COM:443/Path?Q=1#F ", cfg);
    BOOST_REQUIRE(normalized.has_value());
    BOOST_TEST(*normalized == "https://example.com/Path?Q=1#F");
}
