#define BOOST_TEST_MODULE CommandsJobStoreTest
#include <chrono>
#include <cstddef>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/job_store.hpp"
#include "url_shortener/commands/job_types.hpp"
#include "url_shortener/core/clock.hpp"

using url_shortener::commands::is_terminal;
using url_shortener::commands::JobFailure;
using url_shortener::commands::JobPage;
using url_shortener::commands::JobProgress;
using url_shortener::commands::JobRecord;
using url_shortener::commands::JobState;
using url_shortener::commands::JobStore;
using url_shortener::commands::JobSummary;
using url_shortener::commands::JobSummaryPage;
using url_shortener::commands::parse_job_state;
using url_shortener::commands::to_string;

namespace
{

// Compile-time guarantee (threat model T-109 / CWE-400): a JobSummary must never
// carry the heavy result_json payload, while the full JobRecord still does.
// A member-detection idiom turns "the summary grew a result field" into a build
// failure rather than a silent re-introduction of the copy cost.
template <typename T, typename = void>
struct has_result_json : std::false_type
{
};
template <typename T>
struct has_result_json<T, std::void_t<decltype(std::declval<T&>().result_json)>>
    : std::true_type
{
};
static_assert(!has_result_json<JobSummary>::value,
              "JobSummary must not carry result_json (threat model T-109)");
static_assert(has_result_json<JobRecord>::value,
              "JobRecord is expected to carry result_json");

// A clock frozen at the epoch; tests advance it between transitions to prove
// each timestamp is read from the injected IClock, not from the wall clock.
ManualClock make_clock()
{
    return ManualClock(std::chrono::system_clock::time_point{});
}

}  // namespace

// Type: unit (pure data structure).
// Scenario: create() allocates sequential ids j-1, j-2 and leaves each record
// queued with the command/args_digest it was given.
// Boundaries: first two ids; m_next_id starts at 1.
// On failure first check: did create() stop stamping queued, or the id format
// change away from "j-<n>"?
BOOST_AUTO_TEST_CASE(create_allocates_sequential_queued_ids)
{
    ManualClock clock = make_clock();
    JobStore store(clock, 256);

    JobRecord first = store.create("link.import", "0123456789abcdef");
    JobRecord second = store.create("link.export", "fedcba9876543210");

    BOOST_TEST(first.id == "j-1");
    BOOST_TEST(second.id == "j-2");
    BOOST_TEST((first.state == JobState::queued));
    BOOST_TEST((second.state == JobState::queued));
    BOOST_TEST(first.command == "link.import");
    BOOST_TEST(first.args_digest == "0123456789abcdef");
    BOOST_TEST(store.active_count() == 2u);
}

// Type: unit.
// Scenario: the legal lifecycle queued -> running -> succeeded, asserting each
// transition returns true and timestamps come from the advancing clock.
// Boundaries: 5 s between steps; started_at/finished_at set exactly once.
// On failure first check: did mark_running/finish stop stamping, or read the
// wall clock instead of m_clock?
BOOST_AUTO_TEST_CASE(legal_path_queued_running_succeeded_timestamps)
{
    ManualClock clock = make_clock();
    const auto t0 = clock.now();
    JobStore store(clock, 256);

    JobRecord created = store.create("link.import", "digest");
    BOOST_TEST((created.created_at == t0));

    clock.advance(std::chrono::seconds(5));
    const auto t1 = clock.now();
    BOOST_TEST(store.mark_running(created.id));

    clock.advance(std::chrono::seconds(5));
    const auto t2 = clock.now();
    BOOST_TEST(store.finish(created.id, JobState::succeeded,
                            std::string("{\"ok\":true}"), std::nullopt));

    std::optional<JobRecord> got = store.get(created.id);
    BOOST_REQUIRE(got.has_value());
    BOOST_TEST((got->state == JobState::succeeded));
    BOOST_REQUIRE(got->started_at.has_value());
    BOOST_TEST((*got->started_at == t1));
    BOOST_REQUIRE(got->finished_at.has_value());
    BOOST_TEST((*got->finished_at == t2));
    BOOST_REQUIRE(got->result_json.has_value());
    BOOST_TEST(*got->result_json == "{\"ok\":true}");
    BOOST_TEST(store.active_count() == 0u);
}

// Type: unit.
// Scenario: a queued job can go straight to cancelled without ever running, and
// gets a finished_at but no started_at.
// Boundaries: queued -> cancelled (skips running).
// On failure first check: did finish() reject a queued source, or wrongly stamp
// started_at?
BOOST_AUTO_TEST_CASE(queued_to_cancelled_directly)
{
    ManualClock clock = make_clock();
    JobStore store(clock, 256);

    JobRecord created = store.create("link.import", "digest");
    clock.advance(std::chrono::seconds(5));

    BOOST_TEST(store.finish(created.id, JobState::cancelled, std::nullopt,
                            std::nullopt));

    std::optional<JobRecord> got = store.get(created.id);
    BOOST_REQUIRE(got.has_value());
    BOOST_TEST((got->state == JobState::cancelled));
    BOOST_TEST(!got->started_at.has_value());
    BOOST_REQUIRE(got->finished_at.has_value());
    BOOST_TEST(store.active_count() == 0u);
}

// Type: unit.
// Scenario: every illegal transition is rejected and the record is left
// untouched: terminal -> running/progress/finish, and running -> running.
// Boundaries: a terminal record never mutates; mark_running never reverses.
// On failure first check: did a transition stop checking the current state, or
// mutate before validating?
BOOST_AUTO_TEST_CASE(illegal_transitions_rejected_and_record_untouched)
{
    ManualClock clock = make_clock();
    JobStore store(clock, 256);

    JobRecord job = store.create("link.import", "digest");
    BOOST_TEST(store.mark_running(job.id));

    // running -> running (no backward / no re-run).
    BOOST_TEST(!store.mark_running(job.id));

    store.finish(job.id, JobState::succeeded, std::nullopt, std::nullopt);
    std::optional<JobRecord> terminal = store.get(job.id);
    BOOST_REQUIRE(terminal.has_value());
    const auto finished_at = terminal->finished_at;

    // terminal -> anything is a no-op returning false.
    BOOST_TEST(!store.mark_running(job.id));
    BOOST_TEST(!store.update_progress(job.id, JobProgress{1, 10}));
    BOOST_TEST(!store.finish(job.id, JobState::failed, std::nullopt,
                             std::nullopt));

    std::optional<JobRecord> after = store.get(job.id);
    BOOST_REQUIRE(after.has_value());
    BOOST_TEST((after->state == JobState::succeeded));
    BOOST_TEST((after->finished_at == finished_at));
    BOOST_TEST(after->progress.done == 0u);
}

// Type: unit (invalid input).
// Scenario: operations on an unknown id all fail without creating anything.
// Boundaries: id never issued by this store.
// On failure first check: did a mutator start inserting on miss, or get() stop
// returning nullopt?
BOOST_AUTO_TEST_CASE(unknown_id_returns_false_or_nullopt)
{
    ManualClock clock = make_clock();
    JobStore store(clock, 256);

    BOOST_TEST(!store.mark_running("j-999"));
    BOOST_TEST(!store.update_progress("j-999", JobProgress{1, 10}));
    BOOST_TEST(!store.finish("j-999", JobState::succeeded, std::nullopt,
                             std::nullopt));
    BOOST_TEST(!store.get("j-999").has_value());
    BOOST_TEST(store.active_count() == 0u);
}

// Type: unit (invalid input / edge).
// Scenario: update_progress rejects a decreasing done and any non-running job,
// and accepts a monotonic increase.
// Boundaries: done must be >= current done; state must be running.
// On failure first check: did the monotonic guard flip its comparison, or the
// state check drop?
BOOST_AUTO_TEST_CASE(progress_rejects_decrease_and_non_running)
{
    ManualClock clock = make_clock();
    JobStore store(clock, 256);

    JobRecord job = store.create("link.import", "digest");

    // queued: not running yet.
    BOOST_TEST(!store.update_progress(job.id, JobProgress{1, 10}));

    BOOST_TEST(store.mark_running(job.id));
    BOOST_TEST(store.update_progress(job.id, JobProgress{3, 10}));
    BOOST_TEST(store.update_progress(job.id, JobProgress{3, 10}));  // equal ok
    BOOST_TEST(!store.update_progress(job.id, JobProgress{2, 10}));  // decrease

    std::optional<JobRecord> got = store.get(job.id);
    BOOST_REQUIRE(got.has_value());
    BOOST_TEST(got->progress.done == 3u);
    BOOST_TEST(got->progress.total == 10u);

    store.finish(job.id, JobState::succeeded, std::nullopt, std::nullopt);
    // terminal: progress rejected.
    BOOST_TEST(!store.update_progress(job.id, JobProgress{4, 10}));
}

// Type: unit (resource bound, threat model T-86 / T-109).
// Scenario: with max_retained=3, finishing 5 jobs retains only the 3 newest
// terminal records; a still-active job is never evicted.
// Boundaries: table size vs max_retained; terminal-only eviction.
// On failure first check: did evict_locked() evict active jobs, or drop the
// newest instead of the oldest?
BOOST_AUTO_TEST_CASE(retention_evicts_oldest_terminal_only)
{
    ManualClock clock = make_clock();
    JobStore store(clock, 3);

    std::vector<std::string> ids;
    for (int i = 0; i < 5; ++i)
    {
        JobRecord job = store.create("link.import", "digest");
        ids.push_back(job.id);
        store.finish(job.id, JobState::succeeded, std::nullopt, std::nullopt);
    }

    // Oldest two (j-1, j-2) evicted; newest three remain.
    BOOST_TEST(!store.get(ids[0]).has_value());
    BOOST_TEST(!store.get(ids[1]).has_value());
    BOOST_TEST(store.get(ids[2]).has_value());
    BOOST_TEST(store.get(ids[3]).has_value());
    BOOST_TEST(store.get(ids[4]).has_value());

    JobPage all = store.list(std::nullopt, 0, 100);
    BOOST_TEST(all.total == 3u);

    // Active jobs are never evicted even past the cap: 5 running jobs leave the
    // table at 5 (> cap 3) because none are terminal and therefore eligible.
    JobStore store2(clock, 3);
    std::vector<std::string> active;
    for (int i = 0; i < 5; ++i)
    {
        active.push_back(store2.create("link.import", "digest").id);
        store2.mark_running(active.back());
    }
    BOOST_TEST(store2.active_count() == 5u);
    BOOST_TEST(store2.list(std::nullopt, 0, 100).total == 5u);

    // Finishing one active job makes it the only terminal record; with the
    // table still above the cap it is evicted immediately, leaving the four
    // still-active jobs untouched.
    store2.finish(active[0], JobState::succeeded, std::nullopt, std::nullopt);
    BOOST_TEST(store2.list(std::nullopt, 0, 100).total == 4u);
    BOOST_TEST(store2.active_count() == 4u);
    BOOST_TEST(!store2.get(active[0]).has_value());
}

// Type: unit (edge / boundaries).
// Scenario: list() filters by state, returns newest-first, and clamps offset
// and limit at the boundaries (0, exact size, beyond size).
// Boundaries: limit 0 -> empty; offset == size -> empty; offset > size ->
// empty; total always reflects the full filtered match set.
// On failure first check: did the slice math drop total, or the iteration
// order flip to oldest-first?
BOOST_AUTO_TEST_CASE(list_filter_order_and_offset_limit_boundaries)
{
    ManualClock clock = make_clock();
    JobStore store(clock, 256);

    JobRecord a = store.create("link.import", "d");  // j-1
    JobRecord b = store.create("link.import", "d");  // j-2
    JobRecord c = store.create("link.import", "d");  // j-3
    store.mark_running(b.id);  // b running, a and c queued

    // Newest first across all states.
    JobPage all = store.list(std::nullopt, 0, 100);
    BOOST_REQUIRE(all.items.size() == 3u);
    BOOST_TEST(all.total == 3u);
    BOOST_TEST(all.items[0].id == c.id);
    BOOST_TEST(all.items[1].id == b.id);
    BOOST_TEST(all.items[2].id == a.id);

    // Filter by state.
    JobPage queued = store.list(JobState::queued, 0, 100);
    BOOST_TEST(queued.total == 2u);
    BOOST_REQUIRE(queued.items.size() == 2u);
    BOOST_TEST(queued.items[0].id == c.id);
    BOOST_TEST(queued.items[1].id == a.id);

    JobPage running = store.list(JobState::running, 0, 100);
    BOOST_TEST(running.total == 1u);
    BOOST_REQUIRE(running.items.size() == 1u);
    BOOST_TEST(running.items[0].id == b.id);

    // limit 0 -> empty items, total intact.
    JobPage none = store.list(std::nullopt, 0, 0);
    BOOST_TEST(none.items.empty());
    BOOST_TEST(none.total == 3u);

    // offset in range.
    JobPage skip1 = store.list(std::nullopt, 1, 100);
    BOOST_REQUIRE(skip1.items.size() == 2u);
    BOOST_TEST(skip1.items[0].id == b.id);
    BOOST_TEST(skip1.total == 3u);

    // offset == size -> empty, total intact.
    JobPage at_size = store.list(std::nullopt, 3, 100);
    BOOST_TEST(at_size.items.empty());
    BOOST_TEST(at_size.total == 3u);

    // offset beyond size -> empty, total intact.
    JobPage beyond = store.list(std::nullopt, 10, 100);
    BOOST_TEST(beyond.items.empty());
    BOOST_TEST(beyond.total == 3u);
}

// Type: unit (resource safety, threat model T-109 / CWE-400).
// Scenario: for the same filter/offset/limit arguments, list_summaries reports
// the identical total, item count, order, ids, states, commands, progress,
// timestamps and failure codes as list(), across the all-states view, a state
// filter and an offset/limit window -- but each summary is a metadata-only
// projection (no result_json, enforced at compile time above).
// Boundaries: all-states, a running-only filter, and an offset=1/limit=1 window.
// On failure first check: did the two loops drift (one filters/orders/windows
// differently), or did summarize() drop/duplicate a metadata field?
BOOST_AUTO_TEST_CASE(list_summaries_matches_list_metadata_and_order)
{
    ManualClock clock = make_clock();
    JobStore store(clock, 256);

    // j-1 succeeded with a result, j-2 running, j-3 failed with a failure.
    JobRecord a = store.create("link.import", "diga");  // j-1
    store.mark_running(a.id);
    clock.advance(std::chrono::seconds(1));
    store.finish(a.id, JobState::succeeded, std::string("{\"rows\":9}"),
                 std::nullopt);

    JobRecord b = store.create("link.export", "digb");  // j-2
    store.mark_running(b.id);

    JobRecord c = store.create("link.import", "digc");  // j-3
    store.mark_running(c.id);
    clock.advance(std::chrono::seconds(1));
    store.finish(c.id, JobState::failed, std::nullopt,
                 JobFailure{"invalid_field", "row 7 rejected"});

    // Asserts list() and list_summaries() agree item-for-item on every metadata
    // field for one set of arguments.
    const auto same = [&store](std::optional<JobState> filter,
                               std::size_t offset, std::size_t limit) {
        const JobPage full = store.list(filter, offset, limit);
        const JobSummaryPage summ = store.list_summaries(filter, offset, limit);

        BOOST_TEST(summ.total == full.total);
        BOOST_REQUIRE(summ.items.size() == full.items.size());
        for (std::size_t i = 0; i < full.items.size(); ++i) {
            const JobRecord& r = full.items[i];
            const JobSummary& s = summ.items[i];
            BOOST_TEST(s.id == r.id);
            BOOST_TEST(s.command == r.command);
            BOOST_TEST((s.state == r.state));
            BOOST_TEST(s.progress.done == r.progress.done);
            BOOST_TEST(s.progress.total == r.progress.total);
            BOOST_TEST((s.created_at == r.created_at));
            BOOST_TEST((s.started_at == r.started_at));
            BOOST_TEST((s.finished_at == r.finished_at));
            BOOST_TEST(s.failure.has_value() == r.failure.has_value());
            if (s.failure.has_value() && r.failure.has_value()) {
                BOOST_TEST(s.failure->code == r.failure->code);
                BOOST_TEST(s.failure->message == r.failure->message);
            }
        }
    };

    same(std::nullopt, 0, 100);         // all states, newest first
    same(JobState::running, 0, 100);    // state filter
    same(std::nullopt, 1, 1);           // offset/limit window
}

// Type: unit (resource safety, threat model T-109 / CWE-400).
// Scenario: a store holding records with ~1 MiB result_json each still yields
// summaries with the right ids/states and the correct total; the summary type
// has no result field (compile-time, see has_result_json above), so this listing
// never copies the payloads under the store mutex.
// Boundaries: large inline result bodies; the summary is a fixed-shape metadata
// struct regardless of payload size.
// On failure first check: did JobSummary gain a payload field (static_assert),
// or did summarize() stop populating the metadata it must carry?
BOOST_AUTO_TEST_CASE(list_summaries_omits_large_result_payload)
{
    ManualClock clock = make_clock();
    JobStore store(clock, 256);

    const std::string big(1024u * 1024u, 'x');  // ~1 MiB payload per record
    std::vector<std::string> ids;
    for (int i = 0; i < 4; ++i) {
        JobRecord job = store.create("link.export", "digest");
        ids.push_back(job.id);
        store.mark_running(job.id);
        store.finish(job.id, JobState::succeeded,
                     std::string("{\"blob\":\"") + big + "\"}", std::nullopt);
    }

    const JobSummaryPage page = store.list_summaries(std::nullopt, 0, 100);
    BOOST_TEST(page.total == 4u);
    BOOST_REQUIRE(page.items.size() == 4u);
    // Newest first: j-4, j-3, j-2, j-1.
    BOOST_TEST(page.items[0].id == ids[3]);
    BOOST_TEST(page.items[3].id == ids[0]);
    for (const JobSummary& summary : page.items) {
        BOOST_TEST((summary.state == JobState::succeeded));
        BOOST_TEST(summary.command == "link.export");
    }
}

// Type: unit.
// Scenario: the free helpers to_string/is_terminal/parse_job_state behave per
// catalog; an unknown name parses to nullopt.
// Boundaries: every enumerator; terminal set is {succeeded, failed, cancelled}.
// On failure first check: did a name drift from the envelope table, or is_
// terminal miscategorize a state?
BOOST_AUTO_TEST_CASE(state_helpers_names_terminal_and_parse)
{
    BOOST_TEST(to_string(JobState::queued) == "queued");
    BOOST_TEST(to_string(JobState::running) == "running");
    BOOST_TEST(to_string(JobState::succeeded) == "succeeded");
    BOOST_TEST(to_string(JobState::failed) == "failed");
    BOOST_TEST(to_string(JobState::cancelled) == "cancelled");

    BOOST_TEST(!is_terminal(JobState::queued));
    BOOST_TEST(!is_terminal(JobState::running));
    BOOST_TEST(is_terminal(JobState::succeeded));
    BOOST_TEST(is_terminal(JobState::failed));
    BOOST_TEST(is_terminal(JobState::cancelled));

    BOOST_TEST((parse_job_state("running") == std::optional<JobState>(
                                                  JobState::running)));
    BOOST_TEST(!parse_job_state("bogus").has_value());
    BOOST_TEST(!parse_job_state("").has_value());
}

// Type: unit (failure case propagation).
// Scenario: a failed job retains its JobFailure code/message verbatim.
// Boundaries: failure set only on the failed path.
// On failure first check: did finish() drop the failure payload?
BOOST_AUTO_TEST_CASE(failed_job_retains_failure_detail)
{
    ManualClock clock = make_clock();
    JobStore store(clock, 256);

    JobRecord job = store.create("link.import", "digest");
    store.mark_running(job.id);
    JobFailure failure;
    failure.code = "invalid_field";
    failure.message = "row 7 rejected";
    BOOST_TEST(store.finish(job.id, JobState::failed, std::nullopt, failure));

    std::optional<JobRecord> got = store.get(job.id);
    BOOST_REQUIRE(got.has_value());
    BOOST_TEST((got->state == JobState::failed));
    BOOST_REQUIRE(got->failure.has_value());
    BOOST_TEST(got->failure->code == "invalid_field");
    BOOST_TEST(got->failure->message == "row 7 rejected");
}

// Type: unit (concurrency, threat model T-86: ids are unique under contention).
// Scenario: 8 threads each create many jobs concurrently; every id is unique
// and the count is exact.
// Boundaries: shared store under a single mutex; no sleeps.
// On failure first check: is m_next_id guarded by the mutex, or did two threads
// read the same id?
BOOST_AUTO_TEST_CASE(concurrent_create_yields_unique_ids)
{
    ManualClock clock = make_clock();
    JobStore store(clock, 100000);

    constexpr int kThreads = 8;
    constexpr int kPerThread = 500;

    std::vector<std::thread> workers;
    std::vector<std::vector<std::string>> ids(kThreads);
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t)
    {
        workers.emplace_back([&store, &ids, t]() {
            for (int i = 0; i < kPerThread; ++i)
            {
                ids[t].push_back(store.create("link.import", "d").id);
            }
        });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }

    std::set<std::string> unique;
    for (const auto& per_thread : ids)
    {
        for (const auto& id : per_thread)
        {
            unique.insert(id);
        }
    }

    BOOST_TEST(unique.size() ==
               static_cast<std::size_t>(kThreads * kPerThread));
    BOOST_TEST(store.active_count() ==
               static_cast<std::size_t>(kThreads * kPerThread));
}

// Type: unit (invalid input / constructor contract).
// Scenario: constructing with max_retained=0 is a programming error and throws.
// Boundaries: 0 vs the minimum legal value 1.
// On failure first check: did the constructor stop validating max_retained?
BOOST_AUTO_TEST_CASE(zero_max_retained_throws)
{
    ManualClock clock = make_clock();
    BOOST_CHECK_THROW(JobStore(clock, 0), std::invalid_argument);
    BOOST_CHECK_NO_THROW(JobStore(clock, 1));
}
