/**
 * @file ssrf_vectors.hpp
 * @brief The single shared SSRF / URL-validation vector list used by both the
 * `link.create` and `link.import` row tests (Milestone 0006, Task 07.0 subtask
 * 20, threat T-40 invariant).
 *
 * A bulk import row must traverse the **same** SSRF / URL / slug validation as
 * `link.create` - there is no second validation path. The parity proof
 * (`76_import_row_validation_parity_test.cpp`) feeds every vector here through
 * both paths and asserts the per-input wire error code is **byte-identical**.
 * Keeping the vocabulary in one header is what makes that a real invariant: if
 * a reviewer adds an SSRF form here, both tests gain it at once and neither can
 * silently diverge.
 *
 * The list is deliberately syntactic SSRF vocabulary (private ranges,
 * localhost, link-local, loopback in several numeric encodings, userinfo
 * tricks, non-http(s) schemes, an over-long URL). Validation is syntactic: no
 * vector is ever resolved or fetched (the parity test records zero lookups).
 */
#pragma once

#include <string>
#include <vector>

namespace ssrf_vectors
{

/// One URL validation vector: a human label, the untrusted target URL, and the
/// expected outcome with `shortener_allow_private_targets == false`.
struct UrlVector
{
    ///< Short human label for the finding message (never a secret).
    std::string name;

    ///< The untrusted `target_url` fed verbatim to both `link.create` and a
    ///< single-row `link.import`. Never resolved or fetched.
    std::string url;

    ///< `true` when this target must be refused `invalid_url` by URL validation
    ///< (every SSRF/private/scheme vector); `false` for a target that must be
    ///< accepted (a public host). The parity test asserts both the code parity
    ///< and this expected outcome, so a vector is not merely "create == import"
    ///< but also provably refused/accepted (07.0/20 review).
    bool expected_refused;
};

/**
 * @brief The shared SSRF / URL vector list.
 *
 * With `shortener_allow_private_targets == false` every entry marked
 * @ref UrlVector::expected_refused must be refused `invalid_url`, and the rest
 * (a public host) must be accepted. The parity test asserts both that
 * `link.create` and `link.import` agree on the code AND the expected outcome,
 * so the list is a positive SSRF-refusal check, not only a parity check.
 *
 * @return A reference to the immutable, function-local vector list.
 */
inline const std::vector<UrlVector>& url_vectors()
{
    static const std::vector<UrlVector> vectors = {
        // Private / loopback / link-local IPv4.
        {"ipv4_loopback", "http://127.0.0.1/admin", true},
        {"ipv4_private_10", "http://10.0.0.1/", true},
        {"ipv4_private_192", "http://192.168.1.1/", true},
        {"ipv4_private_172", "http://172.16.0.1/", true},
        {"ipv4_link_local_metadata",
         "http://169.254.169.254/latest/meta-data",
         true},
        {"ipv4_unspecified", "http://0.0.0.0/", true},

        // Loopback in alternative numeric encodings (07.0/20 review HIGH).
        {"ipv4_decimal_loopback", "http://2130706433/", true},
        {"ipv4_octal_loopback", "http://0177.0.0.1/", true},
        {"ipv4_hex_loopback", "http://0x7f000001/", true},
        {"ipv4_short_form_loopback", "http://127.1/", true},

        // Trailing-dot / percent-encoded-dot forms resolve to 127.0.0.1 in a
        // browser (WHATWG removes the empty last label) (07.0/20 review
        // MEDIUM).
        {"ipv4_trailing_dot_decimal", "http://2130706433./", true},
        {"ipv4_trailing_dot_octal", "http://0177.0.0.1./", true},
        {"ipv4_trailing_dot_octal_single", "http://017700000001./", true},
        {"ipv4_percent_encoded_dot", "http://2130706433%2e/", true},

        // Hostname forms.
        {"hostname_localhost", "http://localhost/", true},
        {"hostname_localhost_fqdn", "http://localhost./", true},

        // IPv6 loopback / ULA / link-local / IPv4-mapped (bracketed).
        {"ipv6_loopback_bracket", "http://[::1]/", true},
        {"ipv6_unspecified", "http://[::]/", true},
        {"ipv6_ula", "http://[fc00::1]/", true},
        {"ipv6_link_local", "http://[fe80::1]/", true},
        {"ipv6_mapped_loopback", "http://[::ffff:127.0.0.1]/", true},

        // IPv6 that embeds a private IPv4 (07.0/20 review LOW):
        // IPv4-compatible,
        // NAT64 64:ff9b::/96 and 6to4 2002::/16.
        {"ipv6_compatible_loopback", "http://[::127.0.0.1]/", true},
        {"ipv6_nat64_loopback", "http://[64:ff9b::7f00:1]/", true},
        {"ipv6_6to4_loopback", "http://[2002:7f00:1::]/", true},

        // Special-use IPv4 ranges (07.0/20 review LOW).
        {"ipv4_cgnat", "http://100.64.0.1/", true},
        {"ipv4_benchmarking", "http://198.18.0.1/", true},
        {"ipv4_multicast", "http://224.0.0.1/", true},
        {"ipv4_reserved", "http://240.0.0.1/", true},
        {"ipv4_broadcast", "http://255.255.255.255/", true},

        // Userinfo `@` tricks: any '@' in the authority is refused.
        {"userinfo_at_private", "http://user@127.0.0.1/", true},
        {"userinfo_at_public", "http://127.0.0.1@example.com/", true},

        // Non-http(s) schemes.
        {"scheme_file", "file:///etc/passwd", true},
        {"scheme_javascript", "javascript:alert(1)", true},
        {"scheme_data", "data:text/plain;base64,QQ==", true},
        {"scheme_ftp", "ftp://example.com/x", true},

        // An over-long URL on a private host: refused either way, with parity.
        {"overlong_private",
         "http://127.0.0.1/" + std::string(4000, 'a'),
         true},

        // A long URL on a PUBLIC host: accepted by both paths (accept parity).
        {"long_public_accepted",
         "http://example.com/" + std::string(4000, 'b'),
         false},

        // Public controls that MUST stay accepted, including a NAT64 / 6to4
        // address whose embedded IPv4 is public (8.8.8.8): the embedded-v4
        // re-check must not over-block a legitimate translated address.
        {"ipv4_public_decimal", "http://134744072/", false},  // 8.8.8.8
        {"ipv6_nat64_public", "http://[64:ff9b::8.8.8.8]/", false},
        {"ipv6_6to4_public",
         "http://[2002:808:808::]/",
         false},  // 6to4 8.8.8.8
    };
    return vectors;
}

}  // namespace ssrf_vectors
