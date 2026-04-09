/** @file 03_url_normalization_test.cpp @brief Unit test: normalization lowercases and strips default ports. */
#define BOOST_TEST_MODULE UrlNormalizationTest
#include <boost/test/unit_test.hpp>
#include "../../../src/url_shortener.cpp"

BOOST_AUTO_TEST_CASE(url_normalization_lowercases_and_strips_default_ports)
{
    ServerConfig cfg;
    cfg.shortener_allow_private_targets = true;
    const auto normalized = normalizeTargetUrl("  HTTPS://Example.COM:443/Path?Q=1#F ", cfg);
    BOOST_REQUIRE(normalized.has_value());
    BOOST_TEST(*normalized == "https://example.com/Path?Q=1#F");
}
