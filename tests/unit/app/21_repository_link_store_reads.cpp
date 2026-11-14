/**
 * @file 21_repository_link_store_reads.cpp
 * @brief Unit tests for app::RepositoryLinkStore read side (subtask 03):
 * findBySlug L1-then-repository with populate-once, negative results not
 * cached, the L1 TTL staleness bound, repository-error-as-not-found
 * (R-5/T-35), fail-closed decode, the single indexed findById GetById
 * lookup (T-36 scan removed), slugExists, invalidateCache (L1 +
 * best-effort ICacheStore, never read for links per T-38), and
 * constructor null-port rejection.
 */
#define BOOST_TEST_MODULE RepositoryLinkStoreReads
#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/expiring_link_cache.hpp>
#include <url_shortener/app/link_record_codec.hpp>
#include <url_shortener/app/repository_link_store.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/storage/errors/cache_error.hpp>
#include <url_shortener/storage/errors/repo_error.hpp>
#include <url_shortener/storage/i_cache_store.hpp>
#include <url_shortener/storage/i_metadata_repository.hpp>
#include <url_shortener/storage/models/link_record.hpp>

using namespace url_shortener;
using namespace url_shortener::app;

#include "repository_link_store_fakes.hpp"

/**
 * [Unit][App] findBySlug miss reads the repository once, populates L1,
 * and the second read never touches the repository.
 *
 * Scenario:
 *   Given a repository holding one record and an empty L1.
 *   When findBySlug is called twice for that slug.
 *   Then the first call reads the repository and returns the link, and
 *   the second call is served from L1 (repository call counter stays 1).
 *
 * If this breaks, first check:
 *   - findBySlug populating L1 on a decode success.
 *   - the L1 hit returning before the repository read.
 */
BOOST_AUTO_TEST_CASE(find_by_slug_populates_l1_and_reads_once)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    repo->put(makeRecord(makeLink("a")));
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock);

    const auto first = store.findBySlug("a");
    BOOST_REQUIRE(first.has_value());
    BOOST_CHECK_EQUAL(first->slug, "a");
    BOOST_CHECK_EQUAL(first->target_url, "https://example.com/a");
    BOOST_CHECK_EQUAL(repo->get_calls, 1);

    const auto second = store.findBySlug("a");
    BOOST_REQUIRE(second.has_value());
    BOOST_CHECK_EQUAL(second->slug, "a");
    BOOST_CHECK_EQUAL(repo->get_calls, 1);  // served from L1
}

/**
 * [Unit][App] An absent slug returns nullopt and is not cached, so the
 * next read hits the repository again (no negative caching).
 *
 * If this breaks, first check:
 *   - findBySlug not calling m_l1_->set on a repository miss.
 */
BOOST_AUTO_TEST_CASE(find_by_slug_absent_is_not_cached)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock);

    BOOST_CHECK(!store.findBySlug("ghost").has_value());
    BOOST_CHECK(!store.findBySlug("ghost").has_value());
    BOOST_CHECK_EQUAL(repo->get_calls, 2);  // re-read each time
}

/**
 * [Unit][App] The L1 TTL bounds cross-process staleness: a change made
 * behind the store's back becomes visible after the TTL elapses.
 *
 * Scenario:
 *   Given ttl=5s, a link cached at t0, then the repository record
 *   changed behind the store at t1.
 *   When findBySlug is called at t4 (within TTL) and again at t6 (past
 *   TTL).
 *   Then t4 serves the stale L1 copy and t6 re-reads the repository and
 *   sees the new target.
 *
 * Boundaries:
 *   The read at exactly-or-past ttl is a miss and re-reads; this is the
 *   staleness bound README decision 2 promises.
 *
 * If this breaks, first check:
 *   - L1 lazy expiry at ttl, and findBySlug re-reading on an L1 miss.
 */
BOOST_AUTO_TEST_CASE(find_by_slug_staleness_bounded_by_l1_ttl)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    repo->put(makeRecord(makeLink("a")));
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(5));
    RepositoryLinkStore store(repo, nullptr, l1, clock);

    const auto initial = store.findBySlug("a");
    BOOST_REQUIRE(initial.has_value());
    BOOST_CHECK_EQUAL(initial->target_url, "https://example.com/a");
    BOOST_CHECK_EQUAL(repo->get_calls, 1);

    // Change the record behind the store's back.
    Link changed = makeLink("a");
    changed.target_url = "https://example.com/CHANGED";
    repo->put(makeRecord(changed));

    clock.advance(std::chrono::seconds(4));  // within TTL -> stale L1 hit
    const auto stale = store.findBySlug("a");
    BOOST_REQUIRE(stale.has_value());
    BOOST_CHECK_EQUAL(stale->target_url, "https://example.com/a");
    BOOST_CHECK_EQUAL(repo->get_calls, 1);

    clock.advance(std::chrono::seconds(2));  // t6 -> past TTL, re-read
    const auto fresh = store.findBySlug("a");
    BOOST_REQUIRE(fresh.has_value());
    BOOST_CHECK_EQUAL(fresh->target_url, "https://example.com/CHANGED");
    BOOST_CHECK_EQUAL(repo->get_calls, 2);
}

/**
 * [Unit][App] Transient and permanent repository errors surface as
 * nullopt and cache nothing (R-5 / threat model T-35).
 *
 * If this breaks, first check:
 *   - findBySlug returning nullopt when GetByShortCode sets an error.
 *   - nothing written to L1 on an error (next call re-reads).
 */
BOOST_AUTO_TEST_CASE(find_by_slug_repository_error_is_nullopt)
{
    for (const auto err :
         {::RepoError::transient_failure, ::RepoError::permanent_failure})
    {
        ManualClock clock(kT0);
        auto repo = std::make_shared<FakeMetadataRepository>();
        repo->put(makeRecord(makeLink("a")));
        repo->get_error = err;
        auto l1 =
            std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
        RepositoryLinkStore store(repo, nullptr, l1, clock);

        BOOST_CHECK(!store.findBySlug("a").has_value());
        BOOST_CHECK_EQUAL(l1->get("a").has_value(), false);
        BOOST_CHECK(!store.findBySlug("a").has_value());
        BOOST_CHECK_EQUAL(repo->get_calls, 2);  // nothing cached
    }
}

/**
 * [Unit][App] An undecodable record (malformed lnk_stats) returns
 * nullopt and caches nothing (fail-closed decode).
 *
 * If this breaks, first check:
 *   - findBySlug routing records through record_to_link and dropping
 *     the result on a codec error.
 */
BOOST_AUTO_TEST_CASE(find_by_slug_undecodable_record_is_nullopt)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    ::LinkRecord bad = makeRecord(makeLink("a"));
    bad.attributes[std::string(attr_stats)] = "total=notanumber";
    repo->put(bad);
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock);

    BOOST_CHECK(!store.findBySlug("a").has_value());
    BOOST_CHECK(!l1->get("a").has_value());
}

/**
 * [Unit][App] The configured ICacheStore is never read for link data
 * (threat model T-38): a poisoned cache cannot answer findBySlug.
 *
 * If this breaks, first check:
 *   - findBySlug must not call m_cache_->Get anywhere.
 */
BOOST_AUTO_TEST_CASE(find_by_slug_never_reads_icachestore)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    repo->put(makeRecord(makeLink("a")));
    auto cache = std::make_shared<RecordingCacheStore>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, cache, l1, clock);

    BOOST_CHECK(store.findBySlug("a").has_value());
    BOOST_CHECK(!store.findBySlug("missing").has_value());
    BOOST_CHECK_EQUAL(cache->get_calls, 0);
}

/**
 * [Unit][App] findById issues exactly one GetById call, returns the
 * matching link, and populates L1 (a later findBySlug of its slug is an
 * L1 hit).
 *
 * Scenario:
 *   Given a repository holding one record and an empty L1.
 *   When findById is called for its id, then findBySlug for its slug.
 *   Then GetById is called exactly once and the slug lookup is served
 *   from L1 without a GetByShortCode call.
 *
 * If this breaks, first check:
 *   - findById calling m_repo_->GetById once (no ListLinks).
 *   - findById writing the decoded match to L1 under its slug.
 */
BOOST_AUTO_TEST_CASE(find_by_id_found)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    repo->put(makeRecord(makeLink("a")));
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock);

    const auto found = store.findById("id-a");
    BOOST_REQUIRE(found.has_value());
    BOOST_CHECK_EQUAL(found->slug, "a");
    BOOST_CHECK_EQUAL(found->target_url, "https://example.com/a");
    BOOST_CHECK_EQUAL(repo->get_by_id_calls, 1);
    BOOST_CHECK_EQUAL(repo->list_calls, 0);

    // The match was written to L1: a slug lookup is served without a
    // GetByShortCode call.
    const auto by_slug = store.findBySlug("a");
    BOOST_REQUIRE(by_slug.has_value());
    BOOST_CHECK_EQUAL(repo->get_calls, 0);
}

/**
 * [Unit][App] findById for an absent id returns nullopt (one GetById
 * call, nothing cached).
 *
 * If this breaks, first check:
 *   - findById returning nullopt when GetById finds no record.
 */
BOOST_AUTO_TEST_CASE(find_by_id_absent)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    repo->put(makeRecord(makeLink("a")));
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock);

    BOOST_CHECK(!store.findById("id-missing").has_value());
    BOOST_CHECK_EQUAL(repo->get_by_id_calls, 1);
}

/**
 * [Unit][App] findById returns an inactive/soft-deleted record (status
 * is not filtered), matching the port contract.
 *
 * If this breaks, first check:
 *   - GetById (not a status-filtered query) backing findById.
 */
BOOST_AUTO_TEST_CASE(find_by_id_returns_inactive_record)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    Link deleted = makeLink("gone");
    deleted.deleted_at = "2026-02-02T00:00:00Z";
    deleted.enabled = false;
    repo->put(makeRecord(deleted));
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock);

    const auto found = store.findById("id-gone");
    BOOST_REQUIRE(found.has_value());
    BOOST_CHECK_EQUAL(found->slug, "gone");
    BOOST_CHECK(found->deleted_at.has_value());
    BOOST_CHECK(!found->enabled);
}

/**
 * [Unit][App] A backend error from GetById surfaces as nullopt and
 * caches nothing (R-5 / threat model T-35).
 *
 * If this breaks, first check:
 *   - findById returning nullopt when GetById sets an error.
 *   - nothing written to L1 on an error.
 */
BOOST_AUTO_TEST_CASE(find_by_id_backend_error_is_nullopt)
{
    for (const auto err :
         {::RepoError::transient_failure, ::RepoError::permanent_failure})
    {
        ManualClock clock(kT0);
        auto repo = std::make_shared<FakeMetadataRepository>();
        repo->put(makeRecord(makeLink("a")));
        repo->get_by_id_error = err;
        auto l1 =
            std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
        RepositoryLinkStore store(repo, nullptr, l1, clock);

        BOOST_CHECK(!store.findById("id-a").has_value());
        BOOST_CHECK(!l1->get("a").has_value());
    }
}

/**
 * [Unit][App] findById on an empty id short-circuits without a GetById
 * call.
 *
 * If this breaks, first check:
 *   - the early empty-id return before any GetById call.
 */
BOOST_AUTO_TEST_CASE(find_by_id_empty_id_is_nullopt)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    repo->put(makeRecord(makeLink("a")));
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock);

    BOOST_CHECK(!store.findById("").has_value());
    BOOST_CHECK_EQUAL(repo->get_by_id_calls, 0);
}

/**
 * [Unit][App] slugExists reports true, false, and false-on-error.
 *
 * If this breaks, first check:
 *   - slugExists mapping an Exists backend error to false.
 */
BOOST_AUTO_TEST_CASE(slug_exists_true_false_error)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock);

    repo->exists_result = true;
    BOOST_CHECK(store.slugExists("a"));

    repo->exists_result = false;
    BOOST_CHECK(!store.slugExists("a"));

    repo->exists_error = ::RepoError::transient_failure;
    BOOST_CHECK(!store.slugExists("a"));
}

/**
 * [Unit][App] invalidateCache erases L1 and calls ICacheStore::Delete
 * once; a null cache does not crash; a Delete failure does not throw.
 *
 * If this breaks, first check:
 *   - invalidateCache calling m_l1_->erase then m_cache_->Delete once.
 *   - the null-cache branch and the ignored Delete result.
 */
BOOST_AUTO_TEST_CASE(invalidate_cache_erases_l1_and_deletes_once)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    repo->put(makeRecord(makeLink("a")));
    auto cache = std::make_shared<RecordingCacheStore>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, cache, l1, clock);

    BOOST_REQUIRE(store.findBySlug("a").has_value());
    BOOST_CHECK(l1->get("a").has_value());

    store.invalidateCache("a");
    BOOST_CHECK(!l1->get("a").has_value());
    BOOST_CHECK_EQUAL(cache->delete_calls, 1);
    BOOST_CHECK_EQUAL(cache->last_deleted_key, "a");

    // A Delete failure is swallowed (cache-aside contract).
    cache->delete_fails = true;
    BOOST_CHECK_NO_THROW(store.invalidateCache("a"));
    BOOST_CHECK_EQUAL(cache->delete_calls, 2);

    // A null configured cache does not crash.
    RepositoryLinkStore no_cache(repo, nullptr, l1, clock);
    BOOST_CHECK_NO_THROW(no_cache.invalidateCache("a"));
}

/**
 * [Unit][App] The constructor rejects a null repository or null L1 with
 * std::invalid_argument (startup programming error).
 *
 * If this breaks, first check:
 *   - the constructor null checks and their argument order.
 */
BOOST_AUTO_TEST_CASE(constructor_rejects_null_ports)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));

    BOOST_CHECK_THROW(RepositoryLinkStore(nullptr, nullptr, l1, clock),
                      std::invalid_argument);
    BOOST_CHECK_THROW(RepositoryLinkStore(repo, nullptr, nullptr, clock),
                      std::invalid_argument);
}
