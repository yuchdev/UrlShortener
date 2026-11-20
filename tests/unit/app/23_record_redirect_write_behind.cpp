/**
 * @file 23_record_redirect_write_behind.cpp
 * @brief Unit tests for the subtask-05 write-behind redirect-stats path:
 * the default `ILinkStore::recordRedirect` (legacy parity) and the
 * `app::RepositoryLinkStore` override plus `flush_stats`,
 * `start_stats_flusher`, `stop_stats_flusher`.
 *
 * Coverage:
 *   - Default method writes once per recordRedirect with +1 counters.
 *   - No repository I/O on the request thread (write-behind).
 *   - Coalescing across redirects into one UpdateLink per flush.
 *   - L1 TTL expiry and size eviction never regress counts
 *     (merge-on-load).
 *   - Lost-update regression: a flush never reverts a concurrent
 *     disable/soft-delete (narrow lnk_stats-only write of a fresh record).
 *   - L1 is not invalidated by recordRedirect; a hit returns the bumped
 *     counters with zero repository reads.
 *   - Hot-link TTL is not refreshed (patch, not set) so a cross-process
 *     disable becomes visible within one L1 TTL (threat model T-32).
 *   - Repo failure on flush re-queues the delta and persists exactly once
 *     after recovery; a deleted-from-repo link drops its delta.
 *   - The flusher thread persists on its own interval; shutdown/destructor
 *     flush once; thread-safety smoke over 4 threads.
 *   - Follow-up hardening (02.0/05 security review):
 *     * Fix 1: the per-slug flush read-modify-write is serialized with
 *       `update` by the store write mutex, so a concurrent in-process
 *       disable committed during the flush window is not reverted.
 *     * Fix 2: a repository that throws from `UpdateLink` does not take
 *       down an explicit flush, the flusher thread, or the destructor.
 *     * Fix 4: the final (shutdown) flush is time-boxed by
 *       `shutdown_flush_budget`; deltas still pending past the deadline are
 *       dropped and teardown returns promptly.
 */
#define BOOST_TEST_MODULE RecordRedirectWriteBehind
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/expiring_link_cache.hpp>
#include <url_shortener/app/link_record_codec.hpp>
#include <url_shortener/app/repository_link_store.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/storage/errors/repo_error.hpp>

#include "repository_link_store_fakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;

namespace
{

/// Options selecting write-behind mode with a long interval (no timer
/// fires during a test; flushes are driven explicitly via flush_stats).
RepositoryLinkStoreOptions writeBehind()
{
    RepositoryLinkStoreOptions options;
    options.stats_flush_interval = std::chrono::seconds(30);
    return options;
}

/// Options selecting write-through mode (in-memory backend).
RepositoryLinkStoreOptions writeThrough()
{
    RepositoryLinkStoreOptions options;
    options.stats_flush_interval = std::chrono::seconds(0);
    return options;
}

/**
 * @brief Thread-safe counting wrapper over `FakeMetadataRepository`.
 *
 * Counts `GetByShortCode` / `UpdateLink` with atomics so the flusher-thread
 * test can poll them from another thread, and delegates everything else to
 * `inner` (whose public knobs — `update_error`, `get_error`, `records` — the
 * tests reach through `repo->inner`).
 */
class CountingMetadataRepository final : public ::IMetadataRepository
{
public:
    ///< Delegate repository holding the actual records and error knobs.
    std::shared_ptr<FakeMetadataRepository> inner =
        std::make_shared<FakeMetadataRepository>();

    ///< Number of `GetByShortCode` calls (atomic for cross-thread reads).
    mutable std::atomic<int> get_calls {0};

    ///< Number of `UpdateLink` calls (atomic for cross-thread reads).
    mutable std::atomic<int> update_calls {0};

    bool CreateLink(const ::CreateLinkRequest& request,
                    const std::string& id,
                    const std::chrono::system_clock::time_point& now,
                    ::RepoError* error = nullptr) override
    {
        return inner->CreateLink(request, id, now, error);
    }

    std::optional<::LinkRecord> GetByShortCode(
        const std::string& short_code,
        ::RepoError* error = nullptr) const override
    {
        ++get_calls;
        return inner->GetByShortCode(short_code, error);
    }

    std::optional<::LinkRecord> GetById(
        const std::string& id, ::RepoError* error = nullptr) const override
    {
        return inner->GetById(id, error);
    }

    bool UpdateLink(const ::LinkRecord& record,
                    ::RepoError* error = nullptr) override
    {
        ++update_calls;
        return inner->UpdateLink(record, error);
    }

    bool DeleteLink(const std::string& slug,
                    ::RepoError* error = nullptr) override
    {
        return inner->DeleteLink(slug, error);
    }

    std::vector<::LinkRecord> ListLinks(
        const ::ListLinksQuery& query,
        ::RepoError* error = nullptr) const override
    {
        return inner->ListLinks(query, error);
    }

    bool Exists(const std::string& slug,
                ::RepoError* error = nullptr) const override
    {
        return inner->Exists(slug, error);
    }
};

/// Reads the persisted total_redirects for `slug` from a fake repo.
std::uint64_t persistedTotal(FakeMetadataRepository& repo,
                             const std::string& slug)
{
    // snapshot() copies the record under the fake's internal mutex, so this
    // read is safe to run while the flusher thread mutates the same record
    // through UpdateLink (the plain find() hands out a pointer into the
    // records vector and would race).
    const std::optional<::LinkRecord> record = repo.snapshot(slug);
    if (!record.has_value()) {
        return 0;
    }
    const auto stats = get_stats_attribute(record->attributes);
    return stats.has_value() ? stats->total_redirects : 0;
}

/// Reads the persisted total_redirects for `slug` via the counting wrapper.
std::uint64_t persistedTotal(CountingMetadataRepository& repo,
                             const std::string& slug)
{
    return persistedTotal(*repo.inner, slug);
}

/**
 * @brief Minimal `ILinkStore` that only implements `update`, to exercise
 * the default (legacy-parity) `ILinkStore::recordRedirect`.
 */
class UpdateOnlyStore final : public ILinkStore
{
public:
    ///< Number of `update` calls the default recordRedirect made.
    int update_calls = 0;

    ///< Last link passed to `update` (carries the bumped counters).
    Link last_update;

    bool create(const Link&, AppError*) override { return true; }
    std::optional<Link> findBySlug(const std::string&) const override
    {
        return std::nullopt;
    }
    std::optional<Link> findById(const std::string&) const override
    {
        return std::nullopt;
    }
    bool slugExists(const std::string&) const override { return false; }
    void invalidateCache(const std::string&) override {}
    bool update(const Link& link, AppError*) override
    {
        ++update_calls;
        last_update = link;
        return true;
    }
    // recordRedirect inherited (non-pure default).
};

}  // namespace

/**
 * [Unit][App] The default ILinkStore::recordRedirect reproduces the legacy
 * handler behavior: one update per call with all three counters +1.
 *
 * Scenario:
 *   Given a store that only implements update, with a link at total=0.
 *   When recordRedirect is called once.
 *   Then update runs exactly once and carries total/24h/7d == 1 and a
 *   non-empty last_accessed_at.
 *
 * If this breaks, first check:
 *   - The default body in link_command_service.cpp still increments all
 *     three counters and calls update.
 */
BOOST_AUTO_TEST_CASE(default_record_redirect_updates_once_with_increment)
{
    UpdateOnlyStore store;
    Link link = makeLink("aaa");

    BOOST_REQUIRE(store.recordRedirect(link));
    BOOST_CHECK_EQUAL(store.update_calls, 1);
    BOOST_CHECK_EQUAL(store.last_update.stats.total_redirects, 1u);
    BOOST_CHECK_EQUAL(store.last_update.stats.redirects_24h, 1u);
    BOOST_CHECK_EQUAL(store.last_update.stats.redirects_7d, 1u);
    BOOST_CHECK(store.last_update.stats.last_accessed_at.has_value());
}

/**
 * [Unit][App] No repository I/O on the request thread: 1000 recordRedirect
 * calls in write-behind mode cause zero GetByShortCode/UpdateLink (C10).
 *
 * If this breaks, first check:
 *   - recordRedirect only touches m_pending_ and m_l1_, never m_repo_, when
 *     stats_flush_interval > 0.
 */
BOOST_AUTO_TEST_CASE(record_redirect_does_no_repository_io)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<CountingMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock, writeBehind());

    BOOST_REQUIRE(store.create(makeLink("aaa")));
    for (int i = 0; i < 1000; ++i) {
        BOOST_REQUIRE(store.recordRedirect(makeLink("aaa")));
    }

    BOOST_CHECK_EQUAL(repo->get_calls.load(), 0);
    BOOST_CHECK_EQUAL(repo->update_calls.load(), 0);
}

/**
 * [Unit][App] Coalescing: 5 redirects then flush -> one UpdateLink carrying
 * +5; a 6th redirect and a second flush -> persisted total 6.
 *
 * If this breaks, first check:
 *   - flush_stats swaps pending out and writes one UpdateLink per slug.
 *   - the delta is added to the freshly-read persisted stats.
 */
BOOST_AUTO_TEST_CASE(flush_coalesces_deltas_into_one_update)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<CountingMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock, writeBehind());

    BOOST_REQUIRE(store.create(makeLink("aaa")));
    for (int i = 0; i < 5; ++i) {
        store.recordRedirect(makeLink("aaa"));
    }
    store.flush_stats();
    BOOST_CHECK_EQUAL(repo->update_calls.load(), 1);
    BOOST_CHECK_EQUAL(persistedTotal(*repo, "aaa"), 5u);

    store.recordRedirect(makeLink("aaa"));
    store.flush_stats();
    BOOST_CHECK_EQUAL(repo->update_calls.load(), 2);
    BOOST_CHECK_EQUAL(persistedTotal(*repo, "aaa"), 6u);
}

/**
 * [Unit][App] An L1 TTL expiry does not regress counts: the persisted value
 * plus the pending delta is returned on reload (merge-on-load).
 *
 * If this breaks, first check:
 *   - findBySlug merges m_pending_ into the decoded stats before caching.
 */
BOOST_AUTO_TEST_CASE(l1_ttl_expiry_does_not_regress_counts)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<CountingMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(5));
    RepositoryLinkStore store(repo, nullptr, l1, clock, writeBehind());

    BOOST_REQUIRE(store.create(makeLink("aaa")));  // persisted total = 0
    for (int i = 0; i < 3; ++i) {
        store.recordRedirect(makeLink("aaa"));
    }

    clock.advance(std::chrono::seconds(6));  // L1 entry expired
    const auto reloaded = store.findBySlug("aaa");
    BOOST_REQUIRE(reloaded.has_value());
    BOOST_CHECK_EQUAL(reloaded->stats.total_redirects, 3u);

    store.recordRedirect(makeLink("aaa"));
    store.recordRedirect(makeLink("aaa"));
    store.flush_stats();
    BOOST_CHECK_EQUAL(persistedTotal(*repo, "aaa"), 5u);
}

/**
 * [Unit][App] A size eviction (max_entries = 1, two alternating slugs) does
 * not regress counts: each slug's persisted value includes its delta.
 *
 * If this breaks, first check:
 *   - merge-on-load runs on every repository load, not just TTL expiry.
 */
BOOST_AUTO_TEST_CASE(l1_size_eviction_does_not_regress_counts)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<CountingMetadataRepository>();
    auto l1 = std::make_shared<ExpiringLinkCache>(
        clock, std::chrono::seconds(0), /*max_entries=*/1);
    RepositoryLinkStore store(repo, nullptr, l1, clock, writeBehind());

    BOOST_REQUIRE(store.create(makeLink("aaa")));
    BOOST_REQUIRE(store.create(makeLink("bbb")));

    // Alternating redirects; the size-1 L1 keeps evicting the other slug.
    store.recordRedirect(makeLink("aaa"));
    store.recordRedirect(makeLink("bbb"));
    store.recordRedirect(makeLink("aaa"));

    store.flush_stats();
    BOOST_CHECK_EQUAL(persistedTotal(*repo, "aaa"), 2u);
    BOOST_CHECK_EQUAL(persistedTotal(*repo, "bbb"), 1u);
}

/**
 * [Unit][App] Lost-update regression: a redirect's delta cannot revert a
 * concurrent disable. After disable + flush the record stays disabled and
 * carries the new counters.
 *
 * If this breaks, first check:
 *   - flush_stats reads the record fresh and writes only lnk_stats via
 *     set_stats_attribute, never a stale full record.
 */
BOOST_AUTO_TEST_CASE(flush_does_not_revert_concurrent_disable)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<CountingMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock, writeBehind());

    BOOST_REQUIRE(store.create(makeLink("aaa")));
    store.recordRedirect(makeLink("aaa"));

    Link disabled = makeLink("aaa");
    disabled.enabled = false;
    BOOST_REQUIRE(store.update(disabled));

    store.flush_stats();

    const auto reloaded = store.findBySlug("aaa");
    BOOST_REQUIRE(reloaded.has_value());
    BOOST_CHECK_EQUAL(reloaded->enabled, false);
    BOOST_CHECK_EQUAL(persistedTotal(*repo, "aaa"), 1u);
}

/**
 * [Unit][App] Lost-update regression, soft delete: a soft-delete survives a
 * flush that persists a pending redirect delta.
 *
 * If this breaks, first check:
 *   - the codec packs lnk_deleted and flush preserves every non-stats
 *     attribute of the fresh record.
 */
BOOST_AUTO_TEST_CASE(flush_does_not_revert_concurrent_soft_delete)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<CountingMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock, writeBehind());

    BOOST_REQUIRE(store.create(makeLink("aaa")));
    store.recordRedirect(makeLink("aaa"));

    Link deleted = makeLink("aaa");
    deleted.deleted_at = "2026-02-02T00:00:00Z";
    BOOST_REQUIRE(store.update(deleted));

    store.flush_stats();

    const auto reloaded = store.findBySlug("aaa");
    BOOST_REQUIRE(reloaded.has_value());
    BOOST_REQUIRE(reloaded->deleted_at.has_value());
    BOOST_CHECK_EQUAL(*reloaded->deleted_at, "2026-02-02T00:00:00Z");
    BOOST_CHECK_EQUAL(persistedTotal(*repo, "aaa"), 1u);
}

/**
 * [Unit][App] L1 is not invalidated by recordRedirect: a subsequent
 * findBySlug is an L1 hit (zero repository reads) that returns the bumped
 * counters.
 *
 * If this breaks, first check:
 *   - recordRedirect uses patch, which neither erases nor reloads the entry.
 */
BOOST_AUTO_TEST_CASE(record_redirect_keeps_l1_hot_with_incremented_counters)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<CountingMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock, writeBehind());

    BOOST_REQUIRE(store.create(makeLink("aaa")));  // populates L1
    const int reads_before = repo->get_calls.load();

    store.recordRedirect(makeLink("aaa"));
    const auto hit = store.findBySlug("aaa");
    BOOST_REQUIRE(hit.has_value());
    BOOST_CHECK_EQUAL(hit->stats.total_redirects, 1u);
    BOOST_CHECK_EQUAL(repo->get_calls.load(), reads_before);  // no repo read
}

/**
 * [Unit][App] Hot-link TTL is not refreshed (patch, not set): continuous
 * recordRedirect does not keep a stale active copy alive, so a
 * cross-process disable becomes visible one L1 TTL after the load (T-32).
 *
 * Scenario:
 *   Given a slug loaded into a 5 s L1 at t0.
 *   When recordRedirect is called at t1..t4 and the repository record is
 *   replaced out-of-band with a disabled one.
 *   Then at t6 (> ttl after the t0 load) findBySlug re-reads the repository
 *   and returns the disabled record. (With `set`, the t4 bump would have
 *   re-anchored the TTL and the entry would still be live and active.)
 *
 * If this breaks, first check:
 *   - recordRedirect calls m_l1_->patch, never m_l1_->set.
 */
BOOST_AUTO_TEST_CASE(hot_link_ttl_not_refreshed_by_record_redirect)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<CountingMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(5));
    RepositoryLinkStore store(repo, nullptr, l1, clock, writeBehind());

    // Seed the repository directly so L1 is empty until the t0 load.
    repo->inner->put(makeRecord(makeLink("aaa")));

    const auto loaded = store.findBySlug("aaa");  // load at t0, anchors L1
    BOOST_REQUIRE(loaded.has_value());
    BOOST_CHECK_EQUAL(loaded->enabled, true);

    for (int second = 1; second <= 4; ++second) {
        clock.advance(std::chrono::seconds(1));  // t1..t4
        store.recordRedirect(makeLink("aaa"));  // patch, stored_at stays t0
    }

    // Cross-process disable: replace the record out-of-band.
    Link disabled = makeLink("aaa");
    disabled.enabled = false;
    repo->inner->put(makeRecord(disabled));

    clock.advance(std::chrono::seconds(2));  // now t6, > 5 s after t0 load
    const auto after = store.findBySlug("aaa");
    BOOST_REQUIRE(after.has_value());
    BOOST_CHECK_EQUAL(after->enabled, false);  // proves patch, not set
}

/**
 * [Unit][App] A repository failure on flush re-queues the delta; a later
 * flush after recovery persists it exactly once (no loss, no double count).
 *
 * If this breaks, first check:
 *   - flush_stats re-queues the delta on UpdateLink failure via
 *     requeue_delta rather than dropping it or double-counting.
 */
BOOST_AUTO_TEST_CASE(flush_requeues_delta_on_repository_failure)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<CountingMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock, writeBehind());

    BOOST_REQUIRE(store.create(makeLink("aaa")));
    store.recordRedirect(makeLink("aaa"));

    repo->inner->update_error = ::RepoError::transient_failure;
    store.flush_stats();
    BOOST_CHECK_EQUAL(persistedTotal(*repo, "aaa"), 0u);  // not written

    repo->inner->update_error = ::RepoError::none;
    store.flush_stats();
    BOOST_CHECK_EQUAL(persistedTotal(*repo, "aaa"), 1u);  // persisted once

    store.flush_stats();  // nothing left pending
    BOOST_CHECK_EQUAL(persistedTotal(*repo, "aaa"), 1u);  // still exactly one
}

/**
 * [Unit][App] A link deleted from the repository drops its pending delta
 * without error and without an UpdateLink.
 *
 * If this breaks, first check:
 *   - flush_stats drops the delta when GetByShortCode returns absent with
 *     no backend error.
 */
BOOST_AUTO_TEST_CASE(flush_drops_delta_for_deleted_link)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<CountingMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock, writeBehind());

    BOOST_REQUIRE(store.create(makeLink("aaa")));
    store.recordRedirect(makeLink("aaa"));

    repo->inner->records.clear();  // link removed upstream
    store.flush_stats();
    BOOST_CHECK_EQUAL(repo->update_calls.load(), 0);  // no write attempted

    store.flush_stats();  // delta was dropped, nothing re-queued
    BOOST_CHECK_EQUAL(repo->update_calls.load(), 0);
}

/**
 * [Unit][App] flush_stats with nothing pending is a no-op (no repository
 * calls).
 */
BOOST_AUTO_TEST_CASE(flush_with_nothing_pending_is_a_no_op)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<CountingMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock, writeBehind());

    store.flush_stats();
    BOOST_CHECK_EQUAL(repo->get_calls.load(), 0);
    BOOST_CHECK_EQUAL(repo->update_calls.load(), 0);
}

/**
 * [Unit][App] stop_stats_flusher flushes once and joins even when the
 * flusher was never started; the destructor flushes too.
 *
 * If this breaks, first check:
 *   - stop_stats_flusher runs a final flush_stats unconditionally.
 *   - ~RepositoryLinkStore calls stop_stats_flusher.
 */
BOOST_AUTO_TEST_CASE(stop_and_destructor_flush_pending_deltas)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<CountingMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));

    {
        RepositoryLinkStore store(repo, nullptr, l1, clock, writeBehind());
        BOOST_REQUIRE(store.create(makeLink("aaa")));
        store.recordRedirect(makeLink("aaa"));
        store.stop_stats_flusher();  // flushes once, no thread to join
        BOOST_CHECK_EQUAL(persistedTotal(*repo, "aaa"), 1u);

        store.recordRedirect(makeLink("aaa"));  // pending again
    }  // destructor flushes the remaining delta
    BOOST_CHECK_EQUAL(persistedTotal(*repo, "aaa"), 2u);
}

/**
 * [Unit][App] Write-through mode (interval 0) persists from the caller's
 * thread: each recordRedirect writes immediately, no flusher needed.
 *
 * If this breaks, first check:
 *   - recordRedirect calls flush_stats inline when stats_flush_interval == 0.
 */
BOOST_AUTO_TEST_CASE(write_through_mode_persists_inline)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<CountingMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock, writeThrough());

    BOOST_REQUIRE(store.create(makeLink("aaa")));
    store.recordRedirect(makeLink("aaa"));
    BOOST_CHECK_EQUAL(persistedTotal(*repo, "aaa"), 1u);
    store.recordRedirect(makeLink("aaa"));
    BOOST_CHECK_EQUAL(persistedTotal(*repo, "aaa"), 2u);
}

/**
 * [Unit][App] The flusher thread persists a pending delta on its own
 * interval, without an explicit flush_stats call.
 *
 * Uses a real system clock and the smallest non-zero whole-second interval
 * the seconds-typed option allows; the only test that waits on the timer
 * rather than driving it, polling the atomic UpdateLink counter with a
 * bounded timeout.
 *
 * If this breaks, first check:
 *   - start_stats_flusher spawns the thread and flusher_loop calls
 *     flush_stats on each timeout.
 */
BOOST_AUTO_TEST_CASE(flusher_thread_persists_without_explicit_flush)
{
    SystemClock clock;
    auto repo = std::make_shared<CountingMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));

    RepositoryLinkStoreOptions options;
    options.stats_flush_interval = std::chrono::seconds(1);

    RepositoryLinkStore store(repo, nullptr, l1, clock, options);
    BOOST_REQUIRE(store.create(makeLink("aaa")));
    store.recordRedirect(makeLink("aaa"));

    store.start_stats_flusher();

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (repo->update_calls.load() == 0
           && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    store.stop_stats_flusher();

    BOOST_CHECK_GE(repo->update_calls.load(), 1);
    BOOST_CHECK_EQUAL(persistedTotal(*repo, "aaa"), 1u);
}

/**
 * [Unit][App] Thread-safety smoke: 4 threads x 500 recordRedirect over 10
 * slugs, then one flush -> persisted totals sum to 2000, no crash.
 *
 * If this breaks, first check:
 *   - m_stats_mutex_ guards every access to m_pending_ (recordRedirect,
 *     merge, requeue, flush swap).
 */
BOOST_AUTO_TEST_CASE(concurrent_record_redirect_sums_correctly)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<CountingMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock, writeBehind());

    std::vector<std::string> slugs;
    for (int i = 0; i < 10; ++i) {
        const std::string slug = "slug" + std::to_string(i);
        slugs.push_back(slug);
        BOOST_REQUIRE(store.create(makeLink(slug)));
    }

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back(
            [&store, &slugs]
            {
                for (int i = 0; i < 500; ++i) {
                    store.recordRedirect(makeLink(slugs[i % slugs.size()]));
                }
            });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    store.flush_stats();

    std::uint64_t sum = 0;
    for (const auto& slug : slugs) {
        sum += persistedTotal(*repo, slug);
    }
    BOOST_CHECK_EQUAL(sum, 2000u);
}

namespace
{

/**
 * @brief `::IMetadataRepository` wrapper that can (a) fire a one-shot hook
 * inside the first `GetByShortCode` and (b) throw from `UpdateLink`.
 *
 * Used by the follow-up hardening tests to drive a deterministic
 * intra-process race (the hook runs while the flusher holds the store write
 * lock) and to prove a throwing backend never terminates the flusher or
 * teardown. All other behavior delegates to `inner`.
 */
class HookingMetadataRepository final : public ::IMetadataRepository
{
public:
    ///< Delegate holding the actual records and error knobs.
    std::shared_ptr<FakeMetadataRepository> inner =
        std::make_shared<FakeMetadataRepository>();

    ///< Fired exactly once, after the first `GetByShortCode` reads its
    ///< record (so the hook observes the pre-mutation record). Empty by
    ///< default.
    std::function<void(const std::string&)> on_first_get;

    ///< Guards the one-shot `on_first_get` (mutable: set from the const
    ///< `GetByShortCode`).
    mutable std::atomic<bool> get_hook_fired {false};

    ///< When true, `UpdateLink` throws before touching `inner`.
    std::atomic<bool> throw_on_update {false};

    ///< Number of `UpdateLink` entries (incremented even when it throws),
    ///< so a test can confirm the flusher kept retrying after a throw.
    std::atomic<int> update_attempts {0};

    bool CreateLink(const ::CreateLinkRequest& request,
                    const std::string& id,
                    const std::chrono::system_clock::time_point& now,
                    ::RepoError* error = nullptr) override
    {
        return inner->CreateLink(request, id, now, error);
    }

    std::optional<::LinkRecord> GetByShortCode(
        const std::string& short_code,
        ::RepoError* error = nullptr) const override
    {
        auto record = inner->GetByShortCode(short_code, error);
        if (on_first_get && !get_hook_fired.exchange(true)) {
            on_first_get(short_code);
        }
        return record;
    }

    std::optional<::LinkRecord> GetById(
        const std::string& id, ::RepoError* error = nullptr) const override
    {
        return inner->GetById(id, error);
    }

    bool UpdateLink(const ::LinkRecord& record,
                    ::RepoError* error = nullptr) override
    {
        ++update_attempts;
        if (throw_on_update.load()) {
            throw std::runtime_error("UpdateLink failure (test)");
        }
        return inner->UpdateLink(record, error);
    }

    bool DeleteLink(const std::string& slug,
                    ::RepoError* error = nullptr) override
    {
        return inner->DeleteLink(slug, error);
    }

    std::vector<::LinkRecord> ListLinks(
        const ::ListLinksQuery& query,
        ::RepoError* error = nullptr) const override
    {
        return inner->ListLinks(query, error);
    }

    bool Exists(const std::string& slug,
                ::RepoError* error = nullptr) const override
    {
        return inner->Exists(slug, error);
    }
};

}  // namespace

/**
 * [Unit][App] Fix 1 (serialize flush with update): a disable committed by
 * update() while a flush is in its read-modify-write window is NOT reverted.
 *
 * Scenario:
 *   Given a link with one pending redirect delta.
 *   When flush_stats runs and, inside its GetByShortCode (while the store
 *   write mutex is held), a second thread calls store.update(disabled).
 *   Then the disable blocks on the write mutex until the flush's RMW
 *   finishes and lands afterwards, so the final record stays disabled.
 *
 * The hook spins on an atomic the updater sets (a handshake, not a timed
 * sleep) so the updater is already contending for the write mutex before
 * the flush proceeds. With the write mutex removed, the two writes race and
 * the flush can clobber the disable (the former R-1 window), which this test
 * guards against.
 *
 * If this breaks, first check:
 *   - update() and flush_stats' per-slug RMW both hold m_write_mutex_.
 *   - the lock is taken around GetByShortCode..UpdateLink, not just one of
 *     them.
 */
BOOST_AUTO_TEST_CASE(flush_rmw_is_serialized_with_concurrent_disable)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<HookingMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock, writeBehind());

    BOOST_REQUIRE(store.create(makeLink("aaa")));
    store.recordRedirect(makeLink("aaa"));  // one pending delta

    std::atomic<bool> updater_started {false};
    std::thread updater;
    repo->on_first_get = [&](const std::string&)
    {
        // Runs inside the flush's GetByShortCode while m_write_mutex_ is
        // held. Launch a concurrent disable that must block on the write
        // mutex until this flush's RMW completes.
        updater = std::thread(
            [&]
            {
                updater_started.store(true);
                Link disabled = makeLink("aaa");
                disabled.enabled = false;
                store.update(disabled);
            });
        while (!updater_started.load()) {
            std::this_thread::yield();  // handshake, not a sleep
        }
    };

    store.flush_stats();  // holds m_write_mutex_ across the RMW
    updater.join();  // the disable lands strictly after the RMW

    const auto reloaded = store.findBySlug("aaa");
    BOOST_REQUIRE(reloaded.has_value());
    BOOST_CHECK_EQUAL(reloaded->enabled, false);  // disable survived
}

/**
 * [Unit][App] Fix 2 (harden the flusher): a repository that throws from
 * UpdateLink does not propagate out of an explicit flush, and the delta is
 * re-queued and persisted after recovery.
 *
 * If this breaks, first check:
 *   - drain_pending wraps each slug's RMW in catch(...) and requeues.
 */
BOOST_AUTO_TEST_CASE(throwing_update_does_not_escape_flush)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<HookingMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
    RepositoryLinkStore store(repo, nullptr, l1, clock, writeBehind());

    BOOST_REQUIRE(store.create(makeLink("aaa")));
    store.recordRedirect(makeLink("aaa"));

    repo->throw_on_update = true;
    BOOST_CHECK_NO_THROW(store.flush_stats());  // exception contained
    BOOST_CHECK_EQUAL(persistedTotal(*repo->inner, "aaa"), 0u);  // not written

    repo->throw_on_update = false;
    store.flush_stats();  // re-queued delta persists now
    BOOST_CHECK_EQUAL(persistedTotal(*repo->inner, "aaa"), 1u);
}

/**
 * [Unit][App] Fix 2: a throwing UpdateLink does not take down the flusher
 * thread (it keeps retrying) and does not terminate on destruction.
 *
 * Uses a real system clock and the smallest non-zero interval, polling the
 * atomic UpdateLink-attempt counter with a bounded timeout (the same pattern
 * as the plain flusher-thread test).
 *
 * If this breaks, first check:
 *   - flusher_loop wraps flush_stats in catch(...).
 *   - the destructor's final (time-boxed) flush is wrapped too.
 */
BOOST_AUTO_TEST_CASE(throwing_update_does_not_kill_flusher_or_destructor)
{
    SystemClock clock;
    auto repo = std::make_shared<HookingMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));

    RepositoryLinkStoreOptions options;
    options.stats_flush_interval = std::chrono::seconds(1);

    {
        RepositoryLinkStore store(repo, nullptr, l1, clock, options);
        BOOST_REQUIRE(store.create(makeLink("aaa")));
        store.recordRedirect(makeLink("aaa"));

        repo->throw_on_update = true;
        store.start_stats_flusher();

        // The thread must survive the first throw and try again.
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (repo->update_attempts.load() < 2
               && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        BOOST_CHECK_GE(repo->update_attempts.load(), 2);

        // Recovery: once the backend stops throwing the delta persists.
        repo->throw_on_update = false;
        const auto deadline2 =
            std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (persistedTotal(*repo->inner, "aaa") == 0u
               && std::chrono::steady_clock::now() < deadline2)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        BOOST_CHECK_EQUAL(persistedTotal(*repo->inner, "aaa"), 1u);

        // Leave a delta pending and re-arm the throw so the destructor's
        // final flush also hits a throwing UpdateLink; it must not
        // terminate.
        store.recordRedirect(makeLink("aaa"));
        repo->throw_on_update = true;
    }  // destructor -> stop_stats_flusher -> time-boxed final flush

    BOOST_CHECK(true);  // reaching here proves no std::terminate
}

/**
 * [Unit][App] Fix 4 (time-box the shutdown flush): with a zero
 * shutdown_flush_budget every pending slug is past the deadline, so the
 * final flush drops them all (no UpdateLink) and teardown returns promptly.
 *
 * The positive path (default budget persists all pending deltas) is covered
 * by stop_and_destructor_flush_pending_deltas.
 *
 * If this breaks, first check:
 *   - stop_stats_flusher computes a steady-clock deadline from
 *     shutdown_flush_budget and drain_pending breaks the loop past it.
 */
BOOST_AUTO_TEST_CASE(shutdown_flush_time_box_drops_pending_past_budget)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<CountingMetadataRepository>();
    auto l1 =
        std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));

    RepositoryLinkStoreOptions options;
    options.stats_flush_interval = std::chrono::seconds(30);  // write-behind
    options.shutdown_flush_budget = std::chrono::milliseconds(0);  // no budget
    RepositoryLinkStore store(repo, nullptr, l1, clock, options);

    for (int i = 0; i < 5; ++i) {
        const std::string slug = "slug" + std::to_string(i);
        BOOST_REQUIRE(store.create(makeLink(slug)));
        store.recordRedirect(makeLink(slug));
    }

    store.stop_stats_flusher();  // time-boxed final flush, zero budget

    BOOST_CHECK_EQUAL(repo->update_calls.load(), 0);  // nothing persisted
    for (int i = 0; i < 5; ++i) {
        BOOST_CHECK_EQUAL(persistedTotal(*repo, "slug" + std::to_string(i)),
                          0u);
    }
}
