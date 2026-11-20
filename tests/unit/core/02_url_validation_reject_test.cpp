/** @file 02_url_validation_reject_test.cpp @brief Unit test: invalid/unsafe schemes are rejected. */
#define BOOST_TEST_MODULE UrlValidationRejectTest
#include <boost/test/unit_test.hpp>
#include <url_shortener/url_shortener.hpp>

using namespace url_shortener;

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

/**
 * [Unit][Core][Security] Alternative IP encodings cannot bypass the private-
 * target (SSRF) block (07.0/20 review HIGH).
 *
 * Scenario:
 *   Given loopback/private targets written in a non-dotted-decimal encoding -
 *   decimal, hex and octal IPv4, inet_aton short forms, and bracketed IPv6
 *   (including an IPv4-mapped address) - that a string-prefix check misses.
 *   When normalizeTargetUrl() validates them with allow_private_targets off.
 *   Then every one is refused (std::nullopt), exactly as the dotted-decimal and
 *   `localhost` forms already were; and with allow_private_targets on each is
 *   accepted again (behaviour is identical when private targets are allowed).
 *
 * Boundaries:
 *   - numeric canonicalization (parse_ipv4_numeric) for 127.0.0.1 written as
 *     2130706433 / 0x7f000001 / 0177.0.0.1 / 127.1;
 *   - bracketed IPv6 authority split at ']' not an inner ':' so [::1] reaches
 *     the check; inet_pton classification of ::1 / fc00:: / fe80:: /
 *     ::ffff:a.b.c.d.
 *
 * If this breaks:
 *   - A crafted create/update target could reach an internal service.
 *   - First check isPrivateHost's numeric canonicalization and
 *     normalizeTargetUrl's bracketed-authority split.
 */
BOOST_AUTO_TEST_CASE(url_validation_rejects_alternative_ip_encodings)
{
    const std::vector<std::string> private_encoded = {
        "http://2130706433/",  // decimal 127.0.0.1
        "http://0x7f000001/",  // hex 127.0.0.1
        "http://0177.0.0.1/",  // octal first octet, 127.0.0.1
        "http://127.1/",  // inet_aton short form, 127.0.0.1
        "http://[::1]/",  // IPv6 loopback (bracketed)
        "http://[::1]:8080/x",  // IPv6 loopback with a port
        "http://[::]/",  // IPv6 unspecified
        "http://[fc00::1]/",  // IPv6 unique-local
        "http://[fe80::1]/",  // IPv6 link-local
        "http://[::ffff:127.0.0.1]/",  // IPv4-mapped loopback
        "http://[::ffff:169.254.169.254]/",  // IPv4-mapped link-local metadata
    };

    ServerConfig denied;  // shortener_allow_private_targets defaults to false
    ServerConfig allowed;
    allowed.shortener_allow_private_targets = true;

    for (const std::string& url : private_encoded) {
        BOOST_TEST(!normalizeTargetUrl(url, denied).has_value(),
                   "should be refused with private targets denied: " + url);
        BOOST_TEST(normalizeTargetUrl(url, allowed).has_value(),
                   "should be accepted with private targets allowed: " + url);
    }
}

/**
 * [Unit][Core][Security] Public numeric IPs stay accepted; malformed numeric
 * hosts do not crash (07.0/20 review).
 *
 * Scenario:
 *   Given public addresses in numeric encodings (dotted and decimal IPv4, a
 *   public bracketed IPv6) and malformed numeric hosts (an octet/decimal
 *   overflow, a `0x` with no digits, more than four parts).
 *   When normalizeTargetUrl() validates them with allow_private_targets off.
 *   Then the public numeric addresses are accepted, and each malformed numeric
 *   host is handled without a crash.
 *
 * Boundaries:
 *   - public canonicalization must NOT over-block (8.8.8.8 == 134744072);
 *   - a malformed numeric host is treated as a plain hostname (parse returns
 *     no address), so it is accepted as a non-private host - never a crash.
 */
BOOST_AUTO_TEST_CASE(
    url_validation_accepts_public_numeric_and_survives_malformed)
{
    ServerConfig cfg;  // private targets denied

    // Public numeric forms stay accepted.
    for (const std::string& url :
         {std::string("http://8.8.8.8/"),
          std::string("http://134744072/"),  // 8.8.8.8
          std::string("http://[2001:4860:4860::8888]/")})
    {
        BOOST_TEST(normalizeTargetUrl(url, cfg).has_value(),
                   "public numeric host should be accepted: " + url);
    }

    // Malformed numeric hosts must not crash. These parse as "not an IPv4", so
    // they are treated as ordinary (public) hostnames and accepted; the point
    // of the case is that the validator returns cleanly either way.
    for (const std::string& url :
         {std::string("http://999.1.1.1/"),  // octet overflow
          std::string("http://0x/"),  // 0x, no digits
          std::string("http://1.2.3.4.5/"),  // >4 parts
          std::string("http://4294967296/")})  // >2^32-1
    {
        BOOST_CHECK_NO_THROW(static_cast<void>(normalizeTargetUrl(url, cfg)));
    }
}
