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

/// One URL validation vector: a human label and the untrusted target URL.
struct UrlVector
{
    ///< Short human label for the finding message (never a secret).
    std::string name;

    ///< The untrusted `target_url` fed verbatim to both `link.create` and a
    ///< single-row `link.import`. Never resolved or fetched.
    std::string url;
};

/**
 * @brief The shared SSRF / URL vector list.
 *
 * Every entry is expected (with `shortener_allow_private_targets == false`) to
 * be refused by URL validation, except where noted; the parity test asserts
 * only that `link.create` and `link.import` agree on the code, so the list may
 * include accepted targets too (a public host) to prove the accept path is
 * shared as well.
 *
 * @return A reference to the immutable, function-local vector list.
 */
inline const std::vector<UrlVector>& url_vectors()
{
    static const std::vector<UrlVector> vectors = {
        // Private / loopback / link-local IPv4.
        {"ipv4_loopback", "http://127.0.0.1/admin"},
        {"ipv4_private_10", "http://10.0.0.1/"},
        {"ipv4_private_192", "http://192.168.1.1/"},
        {"ipv4_private_172", "http://172.16.0.1/"},
        {"ipv4_link_local_metadata", "http://169.254.169.254/latest/meta-data"},
        {"ipv4_unspecified", "http://0.0.0.0/"},

        // Loopback in alternative numeric encodings.
        {"ipv4_decimal_loopback", "http://2130706433/"},
        {"ipv4_octal_loopback", "http://0177.0.0.1/"},
        {"ipv4_hex_loopback", "http://0x7f000001/"},

        // Hostname forms.
        {"hostname_localhost", "http://localhost/"},

        // IPv6 loopback / ULA / link-local.
        {"ipv6_loopback_bracket", "http://[::1]/"},
        {"ipv6_ula", "http://[fc00::1]/"},
        {"ipv6_link_local", "http://[fe80::1]/"},

        // Userinfo `@` tricks: any '@' in the authority is refused.
        {"userinfo_at_private", "http://user@127.0.0.1/"},
        {"userinfo_at_public", "http://127.0.0.1@example.com/"},

        // Non-http(s) schemes.
        {"scheme_file", "file:///etc/passwd"},
        {"scheme_javascript", "javascript:alert(1)"},
        {"scheme_data", "data:text/plain;base64,QQ=="},
        {"scheme_ftp", "ftp://example.com/x"},

        // An over-long URL on a private host: rejected either way, with parity.
        {"overlong_private", "http://127.0.0.1/" + std::string(4000, 'a')},

        // A long URL on a PUBLIC host: accepted by both paths (accept parity).
        {"long_public_accepted",
         "http://example.com/" + std::string(4000, 'b')},
    };
    return vectors;
}

}  // namespace ssrf_vectors
