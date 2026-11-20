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

/**
 * [Unit][Core][Security] Trailing-dot, FQDN localhost, IPv6-embedded and
 * special-use private targets are refused (07.0/20 review follow-up).
 *
 * Scenario:
 *   Given private targets written with a trailing '.' (which WHATWG strips, so
 *   a browser resolves them to 127.0.0.1), the fully-qualified 'localhost.',
 *   IPv6 addresses that embed a private IPv4 (IPv4-compatible, NAT64, 6to4),
 *   and special-use IPv4 ranges (CGNAT, benchmarking, multicast, reserved,
 *   broadcast).
 *   When normalizeTargetUrl() validates them with allow_private_targets off.
 *   Then each is refused; with allow_private_targets on each is accepted again
 *   (the private-target gate is the only thing that changes).
 *
 * If this breaks:
 *   - A crafted create/update target could reach an internal service.
 *   - First check the trailing-dot strip in isPrivateHost, classify_ipv6's
 *     embedded-v4 branches, and the special-use ranges in is_private_ipv4.
 */
BOOST_AUTO_TEST_CASE(
    url_validation_rejects_trailing_dot_embedded_and_special_use)
{
    ServerConfig denied;  // private targets denied by default
    ServerConfig allowed;
    allowed.shortener_allow_private_targets = true;

    const std::vector<std::string> gated = {
        "http://2130706433./",  // decimal 127.0.0.1 with a trailing dot
        "http://0177.0.0.1./",  // octal 127.0.0.1 with a trailing dot
        "http://017700000001./",  // single octal 127.0.0.1 with a trailing dot
        "http://localhost./",  // FQDN localhost
        "http://[::127.0.0.1]/",  // IPv4-compatible loopback
        "http://[64:ff9b::7f00:1]/",  // NAT64 loopback
        "http://[2002:7f00:1::]/",  // 6to4 loopback
        "http://100.64.0.1/",  // CGNAT 100.64.0.0/10
        "http://198.18.0.1/",  // benchmarking 198.18.0.0/15
        "http://224.0.0.1/",  // multicast 224.0.0.0/4
        "http://240.0.0.1/",  // reserved 240.0.0.0/4
        "http://255.255.255.255/",  // broadcast
    };
    for (const std::string& url : gated) {
        BOOST_TEST(!normalizeTargetUrl(url, denied).has_value(),
                   "should be refused with private targets denied: " + url);
        BOOST_TEST(normalizeTargetUrl(url, allowed).has_value(),
                   "should be accepted with private targets allowed: " + url);
    }

    // Percent-encoding in the host is refused unconditionally (we never decode
    // it), so an encoded dot cannot reach the canonicalizer in either mode.
    for (const ServerConfig& cfg : {denied, allowed}) {
        BOOST_TEST(
            !normalizeTargetUrl("http://2130706433%2e/", cfg).has_value(),
            "percent-encoded dot must be refused regardless of mode");
    }
}

/**
 * [Unit][Core][Security] Public addresses - including a NAT64/6to4 address that
 * embeds a PUBLIC IPv4 - stay accepted after the embedded-v4 and special-use
 * hardening (07.0/20 review follow-up).
 */
BOOST_AUTO_TEST_CASE(url_validation_accepts_public_including_translated)
{
    ServerConfig cfg;  // private targets denied

    for (const std::string& url : {std::string("http://8.8.8.8/"),
                                   std::string("http://1.1.1.1/"),
                                   std::string("http://93.184.216.34/"),
                                   std::string("http://[64:ff9b::8.8.8.8]/"),
                                   std::string("http://[2002:808:808::]/")})
    {
        BOOST_TEST(
            normalizeTargetUrl(url, cfg).has_value(),
            "public (possibly translated) host should be accepted: " + url);
    }
}

/**
 * [Unit][Core][Security] A malformed authority a browser would re-normalize to
 * loopback is refused in BOTH modes; valid registered names stay accepted
 * (07.0/20 review follow-up - strict host allowlist).
 *
 * Scenario:
 *   Given a numeric host followed by a backslash (WHATWG ends the host at '\'
 *   and maps it to '/'), a host with leading whitespace, and a host with a
 *   double trailing dot - all of which a browser collapses to 127.0.0.1.
 *   When normalizeTargetUrl() validates them with private targets either
 *   denied or allowed.
 *   Then each is refused as a malformed URL in BOTH modes (behaviour change:
 *   malformed authorities are rejected even when allow_private_targets is on),
 *   while valid hosts - a single FQDN trailing dot, a punycode host, a
 *   host:port and path/query percent-encoding - stay accepted.
 *
 * If this breaks:
 *   - A '\'/whitespace host could reach loopback after browser normalization.
 *   - First check the authority backslash refusal and is_valid_url_host.
 */
BOOST_AUTO_TEST_CASE(url_validation_rejects_backslash_whitespace_double_dot)
{
    ServerConfig denied;
    ServerConfig allowed;
    allowed.shortener_allow_private_targets = true;

    const std::vector<std::string> malformed = {
        std::string("http://2130706433\\x"),  // backslash ends the host
        std::string("http://0x7f000001\\x"),  // backslash after hex
        std::string("http:// 2130706433/"),  // leading whitespace in host
        std::string("http://2130706433../"),  // double trailing dot
    };
    for (const std::string& url : malformed) {
        BOOST_TEST(!normalizeTargetUrl(url, denied).has_value(),
                   "malformed authority must be refused (denied): " + url);
        BOOST_TEST(!normalizeTargetUrl(url, allowed).has_value(),
                   "malformed authority must be refused (allowed too): " + url);
    }

    // Valid registered names / numeric forms stay accepted in both modes.
    ServerConfig cfg;  // denied is fine; these are public
    for (const std::string& url :
         {std::string("http://example.com/"),
          std::string("http://example.com./"),  // single FQDN trailing dot
          std::string("http://xn--bcher-kva.example/"),  // punycode IDN
          std::string("http://8.8.8.8/"),
          std::string("http://134744072/"),
          std::string("http://[2001:4860:4860::8888]/"),
          std::string("http://example.com:8080/path"),
          std::string("http://example.com/a%20b?q=%2e")})  // path/query percent
    {
        BOOST_TEST(normalizeTargetUrl(url, cfg).has_value(),
                   "valid host should be accepted: " + url);
    }
}
