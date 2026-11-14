/**
 * @file 19_expiring_link_cache.cpp
 * @brief Unit tests for app::ExpiringLinkCache: round trip, TTL boundary,
 * size bound, patch-in-place semantics (threat model T-32), and a
 * thread-safety smoke test.
 */
#define BOOST_TEST_MODULE ExpiringLinkCache
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/expiring_link_cache.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/core/types.hpp>

using namespace url_shortener;
using namespace url_shortener::app;

namespace
{

/**
 * @brief Builds a minimal valid Link with the given slug.
 * @param slug Slug/id/target basis for the link.
 * @return A populated Link suitable for caching.
 */
Link makeLink(const std::string& slug)
{
    Link link;
    link.id = "id-" + slug;
    link.slug = slug;
    link.target_url = "https://example.com/" + slug;
    link.created_at = "2026-01-01T00:00:00Z";
    link.updated_at = link.created_at;
    link.enabled = true;
    link.redirect_type = RedirectType::temporary;
    return link;
}

/// Fixed clock origin for deterministic TTL arithmetic.
const std::chrono::system_clock::time_point kT0 =
    std::chrono::system_clock::time_point(std::chrono::seconds(1'000'000));

}  // namespace

/**
 * [Unit][App] set/get/erase round trip; unknown slug is a miss.
 *
 * Scenario:
 *   Given an ExpiringLinkCache with ttl=0 (never expires).
 *   When a link is set, read, erased, and an unknown slug is read.
 *   Then get returns the stored value, erase removes it, and misses
 *   return nullopt.
 *
 * If this breaks, first check:
 *   - get/set/erase map plumbing under the lock.
 */
BOOST_AUTO_TEST_CASE(set_get_erase_round_trip)
{
    ManualClock clock(kT0);
    ExpiringLinkCache cache(clock, std::chrono::seconds(0));

    BOOST_CHECK(!cache.get("missing").has_value());

    cache.set("a", makeLink("a"));
    const auto got = cache.get("a");
    BOOST_REQUIRE(got.has_value());
    BOOST_CHECK_EQUAL(got->slug, "a");
    BOOST_CHECK_EQUAL(got->target_url, "https://example.com/a");
    BOOST_CHECK_EQUAL(cache.size(), 1u);

    cache.erase("a");
    BOOST_CHECK(!cache.get("a").has_value());
    BOOST_CHECK_EQUAL(cache.size(), 0u);

    // erase of an absent key is a no-op.
    cache.erase("a");
    BOOST_CHECK_EQUAL(cache.size(), 0u);
}

/**
 * [Unit][App] TTL boundary: hit at ttl-1s, miss at exactly ttl; set
 * after expiry refreshes stored_at.
 *
 * Scenario:
 *   Given ttl=5s and a ManualClock.
 *   When reading at t+4 (hit), t+5 (miss, removed), then re-setting
 *   and reading again at its own t+4 / t+5.
 *   Then the entry is live strictly before ttl and expired at exactly
 *   ttl, and a fresh set resets the TTL anchor.
 *
 * If this breaks, first check:
 *   - is_expired boundary (>= ttl) and lazy removal on get.
 *   - set rewriting stored_at to now.
 */
BOOST_AUTO_TEST_CASE(ttl_boundary_and_set_refreshes_stored_at)
{
    ManualClock clock(kT0);
    ExpiringLinkCache cache(clock, std::chrono::seconds(5));

    cache.set("a", makeLink("a"));

    clock.advance(std::chrono::seconds(4));
    BOOST_CHECK(cache.get("a").has_value());

    clock.advance(std::chrono::seconds(1));  // now t0 + 5 == ttl
    BOOST_CHECK(!cache.get("a").has_value());
    BOOST_CHECK_EQUAL(cache.size(), 0u);  // expired entry removed on get

    // set after expiry refreshes stored_at: anchor is now t0 + 5.
    cache.set("a", makeLink("a"));
    clock.advance(std::chrono::seconds(4));  // age 4
    BOOST_CHECK(cache.get("a").has_value());
    clock.advance(std::chrono::seconds(1));  // age 5 == ttl
    BOOST_CHECK(!cache.get("a").has_value());
}

/**
 * [Unit][App] ttl=0 entries never expire.
 *
 * Scenario:
 *   Given ttl=0 and an entry set at t0.
 *   When the clock advances ten years.
 *   Then the entry is still a hit.
 *
 * If this breaks, first check:
 *   - is_expired short-circuit for ttl == 0.
 */
BOOST_AUTO_TEST_CASE(ttl_zero_never_expires)
{
    ManualClock clock(kT0);
    ExpiringLinkCache cache(clock, std::chrono::seconds(0));

    cache.set("a", makeLink("a"));
    clock.advance(std::chrono::seconds(10 * 365 * 24 * 3600));
    BOOST_CHECK(cache.get("a").has_value());
}

/**
 * [Unit][App] A backward clock step expires entries fail-safe (threat
 * model T-32).
 *
 * Scenario:
 *   Given ttl=1h and an entry set at t0.
 *   When the clock first advances 60s (below ttl) then steps backwards
 *   an hour so now < stored_at.
 *   Then the forward read below ttl hits, but after the backward step
 *   get() misses and patch() returns false without invoking the
 *   mutator or inserting anything.
 *
 * Boundaries:
 *   now < stored_at (clock moved backwards), which must be treated as
 *   expired so a clock anomaly can only shorten a lifetime.
 *
 * If this breaks, first check:
 *   - is_expired's `now < e.stored_at` fail-safe branch.
 *   - that get() and patch() both route through is_expired.
 */
BOOST_AUTO_TEST_CASE(backward_clock_step_is_failsafe)
{
    ManualClock clock(kT0);
    ExpiringLinkCache cache(clock, std::chrono::seconds(3600));  // 1h ttl

    cache.set("a", makeLink("a"));

    // A normal forward advance below ttl still hits.
    clock.advance(std::chrono::seconds(60));
    BOOST_CHECK(cache.get("a").has_value());

    // Clock steps backwards an hour: now < stored_at. The entry must be
    // treated as expired rather than gaining an extra hour of life.
    clock.set(kT0 - std::chrono::hours(1));
    BOOST_CHECK(!cache.get("a").has_value());

    // patch() under a backward clock returns false without running the
    // mutator or inserting anything.
    ManualClock clock2(kT0);
    ExpiringLinkCache cache2(clock2, std::chrono::seconds(3600));
    cache2.set("a", makeLink("a"));
    clock2.set(kT0 - std::chrono::hours(1));

    bool mutator_called = false;
    const bool patched =
        cache2.patch("a", [&](Link&) { mutator_called = true; });
    BOOST_CHECK(!patched);
    BOOST_CHECK(!mutator_called);
    BOOST_CHECK(!cache2.get("a").has_value());

    // patch() on an unknown slug under a backward clock inserts nothing.
    BOOST_CHECK(!cache2.patch("ghost", [](Link&) {}));
    BOOST_CHECK_EQUAL(cache2.size(), 0u);
}

/**
 * [Unit][App] Size bound keeps size() <= max_entries on overflow.
 *
 * Scenario:
 *   Given ttl=0 and max_entries=3.
 *   When a 4th distinct slug is set.
 *   Then size() stays <= 3.
 *
 * If this breaks, first check:
 *   - eviction branch on insert when size() >= max_entries.
 */
BOOST_AUTO_TEST_CASE(size_bound_enforced)
{
    ManualClock clock(kT0);
    ExpiringLinkCache cache(clock, std::chrono::seconds(0), 3);

    cache.set("a", makeLink("a"));
    cache.set("b", makeLink("b"));
    cache.set("c", makeLink("c"));
    BOOST_CHECK_EQUAL(cache.size(), 3u);

    cache.set("d", makeLink("d"));
    BOOST_CHECK_LE(cache.size(), 3u);
}

/**
 * [Unit][App] Expired entries are evicted before live ones on overflow.
 *
 * Scenario:
 *   Given ttl=5s and max_entries=3, with a, b set at t0 and c set at
 *   t4, then the clock advanced to t6 (a, b expired; c live).
 *   When a 4th slug d is set.
 *   Then the live entry c survives and size() <= 3.
 *
 * If this breaks, first check:
 *   - the "drop expired first, then one arbitrary" eviction order.
 */
BOOST_AUTO_TEST_CASE(expired_evicted_before_live)
{
    ManualClock clock(kT0);
    ExpiringLinkCache cache(clock, std::chrono::seconds(5), 3);

    cache.set("a", makeLink("a"));
    cache.set("b", makeLink("b"));

    // max_entries (3) <= k_eviction_sample_size (8), so the whole map
    // is within the eviction sample and the expired-first preference
    // is guaranteed to find an expired entry if one exists.
    static_assert(3u <= ExpiringLinkCache::k_eviction_sample_size,
                  "this test relies on the cache fitting in one sample");

    clock.advance(std::chrono::seconds(4));  // t0 + 4
    cache.set("c", makeLink("c"));

    clock.advance(std::chrono::seconds(2));  // t0 + 6: a, b expired; c live
    cache.set("d", makeLink("d"));

    BOOST_CHECK_LE(cache.size(), 3u);
    BOOST_CHECK(cache.get("c").has_value());  // live entry survived
    BOOST_CHECK(cache.get("d").has_value());
}

/**
 * [Unit][App] Size bound is strictly enforced even when max_entries
 * exceeds the eviction sample size (C10 / CWE-400 bounded eviction).
 *
 * Scenario:
 *   Given ttl=0 (no entry ever expires) and max_entries chosen larger
 *   than k_eviction_sample_size, filled exactly to the bound.
 *   When many further distinct slugs are inserted.
 *   Then size() never exceeds max_entries at any point.
 *
 * Boundaries:
 *   max_entries > k_eviction_sample_size, so a full-map sweep is never
 *   an option: eviction samples a bounded prefix and, finding no
 *   expired entry (ttl=0), drops the first sampled entry. Each insert
 *   therefore examines at most k_eviction_sample_size entries — the
 *   O(1)-amortized guarantee — while still holding the strict bound.
 *
 * If this breaks, first check:
 *   - set evicts exactly one entry at the bound (arbitrary fallback
 *     when nothing in the sample is expired).
 *   - the sample loop stops after k_eviction_sample_size iterations.
 */
BOOST_AUTO_TEST_CASE(size_bound_enforced_above_sample_size)
{
    static_assert(ExpiringLinkCache::k_eviction_sample_size < 16u,
                  "this test assumes max_entries exceeds the sample size");

    ManualClock clock(kT0);
    const std::size_t max_entries = 16;
    ExpiringLinkCache cache(clock, std::chrono::seconds(0), max_entries);

    for (std::size_t i = 0; i < max_entries; ++i) {
        cache.set("k" + std::to_string(i), makeLink("k" + std::to_string(i)));
    }
    BOOST_CHECK_EQUAL(cache.size(), max_entries);

    // Every further insert must evict exactly one entry, so size() is
    // pinned at the bound and never grows past it.
    for (std::size_t i = max_entries; i < max_entries + 50; ++i) {
        cache.set("k" + std::to_string(i), makeLink("k" + std::to_string(i)));
        BOOST_CHECK_LE(cache.size(), max_entries);
    }
    BOOST_CHECK_EQUAL(cache.size(), max_entries);
}

/**
 * [Unit][App] clear empties the cache; overwrite replaces the value.
 *
 * Scenario:
 *   Given a cache with one entry overwritten by a second set.
 *   When get is called, then clear is called.
 *   Then get returns the overwritten value and clear empties the map.
 *
 * If this breaks, first check:
 *   - set overwrite path (existing key) and clear.
 */
BOOST_AUTO_TEST_CASE(clear_and_overwrite)
{
    ManualClock clock(kT0);
    ExpiringLinkCache cache(clock, std::chrono::seconds(0));

    Link first = makeLink("a");
    first.target_url = "https://example.com/first";
    cache.set("a", first);

    Link second = makeLink("a");
    second.target_url = "https://example.com/second";
    cache.set("a", second);

    const auto got = cache.get("a");
    BOOST_REQUIRE(got.has_value());
    BOOST_CHECK_EQUAL(got->target_url, "https://example.com/second");
    BOOST_CHECK_EQUAL(cache.size(), 1u);

    cache.clear();
    BOOST_CHECK_EQUAL(cache.size(), 0u);
    BOOST_CHECK(!cache.get("a").has_value());
}

/**
 * [Unit][App] patch preserves stored_at (threat model T-32).
 *
 * Scenario:
 *   Given ttl=5s, set at t0, patch at t3 that bumps a counter.
 *   When reading at exactly t5.
 *   Then patch returned true and mutated the Link, but the read at t5
 *   is a miss (patch did not refresh the TTL).
 *
 * If this breaks, first check:
 *   - patch must not touch entry.stored_at.
 */
BOOST_AUTO_TEST_CASE(patch_preserves_stored_at)
{
    ManualClock clock(kT0);
    ExpiringLinkCache cache(clock, std::chrono::seconds(5));

    cache.set("a", makeLink("a"));

    clock.advance(std::chrono::seconds(3));  // t0 + 3
    const bool patched =
        cache.patch("a", [](Link& l) { l.stats.total_redirects += 1; });
    BOOST_CHECK(patched);

    // The mutation is visible while the entry is live.
    const auto mid = cache.get("a");
    BOOST_REQUIRE(mid.has_value());
    BOOST_CHECK_EQUAL(mid->stats.total_redirects, 1u);

    clock.advance(std::chrono::seconds(2));  // t0 + 5 == ttl
    BOOST_CHECK(!cache.get("a").has_value());
}

/**
 * [Unit][App] repeated patch on a hot entry still expires at the
 * original TTL (threat model T-32).
 *
 * Scenario:
 *   Given ttl=5s, set at t0, patch at t1/t2/t3/t4.
 *   When reading at exactly t5.
 *   Then the read is a miss regardless of patch frequency.
 *
 * If this breaks, first check:
 *   - patch leaving stored_at anchored at the set time.
 */
BOOST_AUTO_TEST_CASE(repeated_patch_still_expires_at_ttl)
{
    ManualClock clock(kT0);
    ExpiringLinkCache cache(clock, std::chrono::seconds(5));

    cache.set("a", makeLink("a"));

    for (int i = 1; i <= 4; ++i) {
        clock.advance(std::chrono::seconds(1));  // t0 + i
        BOOST_CHECK(
            cache.patch("a", [](Link& l) { l.stats.total_redirects += 1; }));
    }

    clock.advance(std::chrono::seconds(1));  // t0 + 5 == ttl
    BOOST_CHECK(!cache.get("a").has_value());
}

/**
 * [Unit][App] patch on a miss or an expired entry never inserts or
 * resurrects (threat model T-32).
 *
 * Scenario:
 *   Given ttl=5s.
 *   When patch is called on an unknown slug, and on an entry past its
 *   TTL.
 *   Then both return false, size() is not grown, and the slug stays
 *   absent on a subsequent get.
 *
 * If this breaks, first check:
 *   - patch returning false without emplace on miss/expired.
 */
BOOST_AUTO_TEST_CASE(patch_on_miss_or_expired_does_not_insert)
{
    ManualClock clock(kT0);
    ExpiringLinkCache cache(clock, std::chrono::seconds(5));

    // Miss: unknown slug.
    BOOST_CHECK(!cache.patch("ghost", [](Link&) {}));
    BOOST_CHECK_EQUAL(cache.size(), 0u);
    BOOST_CHECK(!cache.get("ghost").has_value());

    // Expired: entry past its TTL is not resurrected.
    cache.set("a", makeLink("a"));
    clock.advance(std::chrono::seconds(5));  // t0 + 5 == ttl
    BOOST_CHECK(
        !cache.patch("a", [](Link& l) { l.stats.total_redirects += 1; }));
    BOOST_CHECK(!cache.get("a").has_value());
    BOOST_CHECK_EQUAL(cache.size(), 0u);
}

/**
 * [Unit][App] Thread-safety smoke: concurrent mixed operations never
 * crash and respect the size bound.
 *
 * Scenario:
 *   Given max_entries=64 and 4 threads each doing 1000 mixed
 *   set/get/erase/patch operations.
 *   When all threads join.
 *   Then there was no crash and final size() <= max_entries.
 *
 * If this breaks, first check:
 *   - every public method taking m_mutex_ for the whole operation.
 */
BOOST_AUTO_TEST_CASE(thread_safety_smoke)
{
    SystemClock clock;
    const std::size_t max_entries = 64;
    ExpiringLinkCache cache(clock, std::chrono::seconds(0), max_entries);

    const int per_thread = 1000;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back(
            [&cache, t, per_thread]
            {
                for (int i = 0; i < per_thread; ++i) {
                    const std::string slug =
                        "s" + std::to_string((t * 7 + i) % 128);
                    switch (i % 4) {
                        case 0:
                            cache.set(slug, makeLink(slug));
                            break;
                        case 1:
                            (void)cache.get(slug);
                            break;
                        case 2:
                            cache.erase(slug);
                            break;
                        default:
                            (void)cache.patch(
                                slug,
                                [](Link& l) { l.stats.total_redirects += 1; });
                            break;
                    }
                }
            });
    }
    for (auto& th : threads) {
        th.join();
    }

    BOOST_CHECK_LE(cache.size(), max_entries);
}
