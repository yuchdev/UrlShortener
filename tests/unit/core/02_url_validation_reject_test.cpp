/** @file 02_url_validation_reject_test.cpp @brief Unit test: invalid/unsafe
 * schemes are rejected. */
#define BOOST_TEST_MODULE UrlValidationRejectTest
#include <string>
#include <vector>

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
    BOOST_TEST(
        !normalizeTargetUrl("https://user:pass@example.com", cfg).has_value());
}

/**
 * [Unit][Core][Security] URL validation rejects control bytes anywhere
 * (CWE-113 HTTP response splitting / CWE-93 CRLF injection).
 *
 * Scenario:
 *   Given a target URL with a C0 control byte, DEL, or a Unicode line
 *   separator (U+2028/U+2029/U+0085) injected into either the host or
 *   the path.
 *   When normalizeTargetUrl() validates it.
 *   Then it is rejected (std::nullopt) before any parsing, so a crafted
 *   target can never be stored and later written raw into a redirect
 *   Location header.
 *
 * Boundaries:
 *   - Every forbidden byte/sequence from the fix's rejection set is
 *     exercised in both the host and the path position.
 *   - allow_private_targets is on so the host never short-circuits the
 *     check for an unrelated (SSRF) reason.
 *
 * If this breaks:
 *   - A redirect Location could carry CR/LF and split the response.
 *   - First check containsUnsafeUrlControlBytes and its call at the top
 *     of normalizeTargetUrl.
 */
BOOST_AUTO_TEST_CASE(url_validation_rejects_control_bytes_in_host_and_path)
{
    ServerConfig cfg;
    cfg.shortener_allow_private_targets = true;

    const std::vector<std::string> forbidden = {
        std::string("\r"),  // CR
        std::string("\n"),  // LF
        std::string("\r\n"),  // CRLF
        std::string(1, '\0'),  // NUL
        std::string("\t"),  // TAB
        std::string("\x01"),  // SOH (0x01)
        std::string("\x7f"),  // DEL (0x7F)
        std::string("\xE2\x80\xA8"),  // U+2028 line separator
        std::string("\xE2\x80\xA9"),  // U+2029 paragraph separator
        std::string("\xC2\x85"),  // U+0085 NEL
    };

    for (const auto& bad : forbidden) {
        const std::string in_host = "http://exa" + bad + "mple.com/path";
        const std::string in_path = "http://example.com/pa" + bad + "th";
        BOOST_TEST(!normalizeTargetUrl(in_host, cfg).has_value());
        BOOST_TEST(!normalizeTargetUrl(in_path, cfg).has_value());
    }
}
