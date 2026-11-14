/**
 * @file 21_repository_link_store_reads.cpp
 * @brief Unit tests for app::RepositoryLinkStore read side (subtask 03):
 * findBySlug L1-then-repository with populate-once, negative results not
 * cached, the L1 TTL staleness bound, repository-error-as-not-found
 * (R-5/T-35), fail-closed decode, the bounded findById scan (T-36),
 * slugExists, invalidateCache (L1 + best-effort ICacheStore, never read
 * for links per T-38), and constructor null-port rejection.
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

namespace
{

/// Fixed clock origin for deterministic TTL arithmetic.
const std::chrono::system_clock::time_point kT0 =
    std::chrono::system_clock::time_point(std::chrono::seconds(1'000'000));

/**
 * @brief Builds a minimal valid link with the given slug/id.
 * @param slug Slug for the link; id is derived as `"id-" + slug`.
 * @return A populated link suitable for codec round-tripping.
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

/**
 * @brief Encodes a link to a `::LinkRecord` via the production codec.
 * @param link Link to encode.
 * @return The decodable record (the test aborts if encoding fails).
 */
::LinkRecord makeRecord(const Link& link)
{
    ::LinkRecord record;
    std::string error;
    BOOST_REQUIRE_MESSAGE(link_to_record(link, &record, &error),
                          "link_to_record failed: " << error);
    return record;
}

/**
 * @brief Map-backed `::IMetadataRepository` test double that counts
 * read calls and can inject repository errors.
 *
 * Records are stored in insertion order (so `ListLinks` paginates
 * deterministically) and keyed by `short_code`. `GetByShortCode`,
 * `ListLinks` and `Exists` increment per-method call counters; each has
 * an injectable `::RepoError` that, when non-`none`, is returned
 * instead of a result. Write methods are unused by the read-side tests
 * and are no-ops.
 */
class FakeMetadataRepository final : public ::IMetadataRepository
{
public:
    ///< Records in insertion order; also the pagination order.
    std::vector<::LinkRecord> records;

    ///< Number of `GetByShortCode` calls observed.
    mutable int get_calls = 0;

    ///< Number of `ListLinks` (page) calls observed.
    mutable int list_calls = 0;

    ///< Number of `Exists` calls observed.
    mutable int exists_calls = 0;

    ///< Error returned by `GetByShortCode` when not `none`.
    ::RepoError get_error = ::RepoError::none;

    ///< Error returned by `ListLinks` when not `none`.
    ::RepoError list_error = ::RepoError::none;

    ///< Error returned by `Exists` when not `none`.
    ::RepoError exists_error = ::RepoError::none;

    ///< Result returned by `Exists` when no error is injected.
    bool exists_result = true;

    /**
     * @brief Inserts or overwrites a record by short code.
     * @param record Record to store.
     */
    void put(const ::LinkRecord& record)
    {
        for (auto& existing : records) {
            if (existing.short_code == record.short_code) {
                existing = record;
                return;
            }
        }
        records.push_back(record);
    }

    bool CreateLink(const CreateLinkRequest&,
                    const std::string&,
                    const std::chrono::system_clock::time_point&,
                    ::RepoError* = nullptr) override
    {
        return false;
    }

    std::optional<::LinkRecord> GetByShortCode(
        const std::string& short_code,
        ::RepoError* error = nullptr) const override
    {
        ++get_calls;
        if (get_error != ::RepoError::none) {
            if (error != nullptr) {
                *error = get_error;
            }
            return std::nullopt;
        }
        for (const auto& record : records) {
            if (record.short_code == short_code) {
                return record;
            }
        }
        return std::nullopt;
    }

    bool UpdateLink(const ::LinkRecord&, ::RepoError* = nullptr) override
    {
        return false;
    }

    bool DeleteLink(const std::string&, ::RepoError* = nullptr) override
    {
        return true;
    }

    std::vector<::LinkRecord> ListLinks(
        const ListLinksQuery& query,
        ::RepoError* error = nullptr) const override
    {
        ++list_calls;
        if (list_error != ::RepoError::none) {
            if (error != nullptr) {
                *error = list_error;
            }
            return {};
        }
        std::vector<::LinkRecord> filtered;
        for (const auto& record : records) {
            if (!query.include_inactive && !record.is_active) {
                continue;
            }
            if (!query.include_deleted && record.deleted_at.has_value()) {
                continue;
            }
            filtered.push_back(record);
        }
        std::vector<::LinkRecord> page;
        for (std::size_t i = query.offset;
             i < filtered.size() && page.size() < query.limit;
             ++i)
        {
            page.push_back(filtered[i]);
        }
        return page;
    }

    bool Exists(const std::string&, ::RepoError* error = nullptr) const override
    {
        ++exists_calls;
        if (exists_error != ::RepoError::none) {
            if (error != nullptr) {
                *error = exists_error;
            }
            return false;
        }
        return exists_result;
    }
};

/**
 * @brief `::ICacheStore` test double that records `Delete` calls and
 * proves `Get` is never invoked for link data (threat model T-38).
 *
 * A preset `poison` value is returned from `Get` so a test can assert
 * the store's reads never consult it; `get_calls` must stay zero.
 */
class RecordingCacheStore final : public ::ICacheStore
{
public:
    ///< Number of `Get` calls observed; must remain zero.
    mutable int get_calls = 0;

    ///< Number of `Delete` calls observed.
    int delete_calls = 0;

    ///< Last key passed to `Delete`.
    std::string last_deleted_key;

    ///< When true, `Delete` reports a backend failure.
    bool delete_fails = false;

    std::optional<CacheValue> Get(const std::string&,
                                  ::CacheError* = nullptr) override
    {
        ++get_calls;
        return std::nullopt;
    }

    bool Set(const std::string&,
             const CacheValue&,
             std::optional<std::chrono::seconds>,
             ::CacheError* = nullptr) override
    {
        return true;
    }

    bool Delete(const std::string& key, ::CacheError* error = nullptr) override
    {
        ++delete_calls;
        last_deleted_key = key;
        if (delete_fails) {
            if (error != nullptr) {
                *error = ::CacheError::unavailable;
            }
            return false;
        }
        return true;
    }

    bool ClearByPrefix(const std::string&, ::CacheError* = nullptr) override
    {
        return true;
    }
};

}  // namespace

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
 * [Unit][App] findById matches on the 1st page, on the 3rd page with a
 * 2-record page, returns nullopt when absent, and finds a soft-deleted
 * record (listing includes deleted).
 *
 * If this breaks, first check:
 *   - the ListLinks pagination loop (offset += page) and id match.
 *   - include_inactive/include_deleted both true in the query.
 */
BOOST_AUTO_TEST_CASE(find_by_id_paged_scan)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    for (int i = 0; i < 5; ++i) {
        repo->put(makeRecord(makeLink("s" + std::to_string(i))));
    }
    // Mark the last record soft-deleted (is_active=false, deleted_at set).
    Link deleted = makeLink("s4");
    deleted.deleted_at = "2026-02-02T00:00:00Z";
    deleted.enabled = false;
    repo->put(makeRecord(deleted));

    RepositoryLinkStoreOptions options;
    options.id_scan_page = 2;
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock, options);

    // First page: id-s0 at index 0.
    repo->list_calls = 0;
    const auto first = store.findById("id-s0");
    BOOST_REQUIRE(first.has_value());
    BOOST_CHECK_EQUAL(first->slug, "s0");
    BOOST_CHECK_EQUAL(repo->list_calls, 1);

    // Third page: id-s4 at index 4 (pages [0,1],[2,3],[4]).
    repo->list_calls = 0;
    const auto third = store.findById("id-s4");
    BOOST_REQUIRE(third.has_value());
    BOOST_CHECK_EQUAL(third->slug, "s4");
    BOOST_CHECK(third->deleted_at.has_value());  // soft-deleted still found
    BOOST_CHECK_EQUAL(repo->list_calls, 3);

    // Absent id scans every page and returns nullopt.
    BOOST_CHECK(!store.findById("id-missing").has_value());
}

/**
 * [Unit][App] findById stops after id_scan_max records even when the
 * target is absent (threat model T-36 bounded scan).
 *
 * Scenario:
 *   Given 10 non-matching records, id_scan_page=2 and id_scan_max=4.
 *   When findById is called for an absent id.
 *   Then exactly 2 pages (4 records) are scanned before giving up.
 *
 * If this breaks, first check:
 *   - the id_scan_max guard incrementing per record and bailing.
 */
BOOST_AUTO_TEST_CASE(find_by_id_stops_at_scan_max)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    for (int i = 0; i < 10; ++i) {
        repo->put(makeRecord(makeLink("s" + std::to_string(i))));
    }

    RepositoryLinkStoreOptions options;
    options.id_scan_page = 2;
    options.id_scan_max = 4;
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock, options);

    BOOST_CHECK(!store.findById("id-absent").has_value());
    BOOST_CHECK_EQUAL(repo->list_calls, 2);  // 2 pages x 2 = 4 == id_scan_max
}

/**
 * [Unit][App] findById on an empty id short-circuits without scanning.
 *
 * If this breaks, first check:
 *   - the early empty-id return before any ListLinks call.
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
    BOOST_CHECK_EQUAL(repo->list_calls, 0);
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
