#define BOOST_TEST_MODULE TargetRequiresPeer
#include <boost/test/unit_test.hpp>
#include <url_shortener/http/request_handlers.hpp>

using url_shortener::targetRequiresPeer;

// Type: unit (peer-resolution predicate; redirect fast path must not resolve
//   the peer, contract C10 / threat model T-83).
// Scenario: management-plane targets (/api/...) require the peer; redirect
//   fast-path and other targets do not, so the session skips remote_endpoint()
//   on the hot path.
// Boundaries: the prefix test is exact ("/api/"); "/api" without the trailing
//   slash and slug paths that merely contain "api" are not management plane.
// On failure first check: did the prefix match loosen (e.g. substring instead
//   of prefix), which would start resolving the peer on the redirect path?
BOOST_AUTO_TEST_CASE(management_targets_require_peer)
{
    BOOST_TEST(targetRequiresPeer("/api/v1/links"));
    BOOST_TEST(targetRequiresPeer("/api/v1/links/abc"));
    BOOST_TEST(targetRequiresPeer("/api/v1/short-urls"));
    BOOST_TEST(targetRequiresPeer("/api/v1/links?limit=10"));
}

BOOST_AUTO_TEST_CASE(redirect_and_other_targets_do_not_require_peer)
{
    // Redirect fast path: the peer must never be resolved here (C10 / T-83).
    BOOST_TEST(!targetRequiresPeer("/abc123"));
    BOOST_TEST(!targetRequiresPeer("/r/abc123"));
    BOOST_TEST(!targetRequiresPeer("/"));
    BOOST_TEST(!targetRequiresPeer(""));

    // Near-misses that must stay off the management plane.
    BOOST_TEST(!targetRequiresPeer("/api"));
    BOOST_TEST(!targetRequiresPeer("/apixyz"));
    BOOST_TEST(!targetRequiresPeer("/metrics"));
    BOOST_TEST(!targetRequiresPeer("/healthz"));
    // A slug that merely contains "api" is not a prefix match.
    BOOST_TEST(!targetRequiresPeer("/myapi/thing"));
}
