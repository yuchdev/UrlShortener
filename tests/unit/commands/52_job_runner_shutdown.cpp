#define BOOST_TEST_MODULE CommandsJobRunnerShutdownTest
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/job_runner.hpp"
#include "url_shortener/commands/job_types.hpp"
#include "url_shortener/core/clock.hpp"

using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::IJobContext;
using url_shortener::commands::is_terminal;
using url_shortener::commands::JobBody;
using url_shortener::commands::JobOutcome;
using url_shortener::commands::JobRecord;
using url_shortener::commands::JobRunner;
using url_shortener::commands::JobRunnerOptions;
using url_shortener::commands::JobState;
using url_shortener::commands::JobSubmitResult;

namespace
{

// A clock frozen at the epoch; the runner reads it only for record timestamps
// and never writes it, so concurrent reads from several workers are race-free.
ManualClock make_clock()
{
    return ManualClock(std::chrono::system_clock::time_point{});
}

// A one-shot gate: a body blocks in wait() until the test calls open(). Pure
// condition-variable synchronization, never a timed sleep, so tests order
// events deterministically.
struct Gate
{
    std::mutex mutex;
    std::condition_variable cv;
    bool opened = false;

    void wait()
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this] { return opened; });
    }

    void open()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            opened = true;
        }
        cv.notify_all();
    }
};

// A cooperative body that reports some progress, signals it is running, then
// spins on the cancel flag and returns cancelled with a partial result. Used by
// the shutdown/cancel_all tests to prove a running body observes the flag.
JobBody make_cooperative(std::promise<void>& running, std::uint64_t partial)
{
    return [&running, partial](IJobContext& ctx) {
        ctx.advance(partial);
        running.set_value();
        while (!ctx.cancel_requested())
        {
            std::this_thread::yield();
        }
        JobOutcome out;
        out.ok = false;
        out.cancelled = true;
        out.result_json = "{\"done\":" + std::to_string(partial) + "}";
        return out;
    };
}

}  // namespace

// Type: unit (wait_for_terminal happy path).
// Scenario: wait_for_terminal blocks on a running job and returns the final
//   record once the body finishes (released from another thread) in time.
// Boundaries: non-terminal -> terminal transition observed during the wait.
// On failure first check: did run_job stop signalling m_idle_cv after finish,
//   or did wait_for_terminal stop re-reading the record on wake?
BOOST_AUTO_TEST_CASE(wait_for_terminal_returns_final_record_during_wait)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    Gate gate;
    std::promise<void> running;
    JobBody body = [&gate, &running](IJobContext&) {
        running.set_value();
        gate.wait();
        JobOutcome out;
        out.ok = true;
        out.result_json = "{\"n\":1}";
        return out;
    };

    JobSubmitResult a = runner.submit("link.import", "aaaa", std::move(body));
    BOOST_REQUIRE(a.job.has_value());
    running.get_future().wait();

    std::thread opener([&gate] { gate.open(); });
    std::optional<JobRecord> rec =
        runner.wait_for_terminal(a.job->id, std::chrono::seconds(10));
    opener.join();

    BOOST_REQUIRE(rec.has_value());
    BOOST_TEST(is_terminal(rec->state));
    BOOST_TEST((rec->state == JobState::succeeded));
    BOOST_REQUIRE(rec->result_json.has_value());
    BOOST_TEST(rec->result_json.value() == "{\"n\":1}");
}

// Type: unit (wait_for_terminal timeout / boundary).
// Scenario: a gated job never finishes within the timeout; wait_for_terminal
//   returns the latest non-terminal snapshot, not nullopt.
// Boundaries: 50 ms timeout elapses with the body still running.
// On failure first check: did wait_for_terminal start returning nullopt on
//   timeout instead of the current snapshot, or hang past the deadline?
BOOST_AUTO_TEST_CASE(wait_for_terminal_returns_non_terminal_snapshot_on_timeout)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    Gate gate;
    std::promise<void> running;
    JobBody body = [&gate, &running](IJobContext&) {
        running.set_value();
        gate.wait();
        JobOutcome out;
        out.ok = true;
        return out;
    };
    JobSubmitResult a = runner.submit("link.import", "aaaa", std::move(body));
    BOOST_REQUIRE(a.job.has_value());
    running.get_future().wait();

    std::optional<JobRecord> rec =
        runner.wait_for_terminal(a.job->id, std::chrono::milliseconds(50));
    BOOST_REQUIRE(rec.has_value());
    BOOST_TEST(!is_terminal(rec->state));
    BOOST_TEST((rec->state == JobState::running));

    gate.open();
    std::optional<JobRecord> fin =
        runner.wait_for_terminal(a.job->id, std::chrono::seconds(10));
    BOOST_REQUIRE(fin.has_value());
    BOOST_TEST(is_terminal(fin->state));
}

// Type: unit (invalid input).
// Scenario: wait_for_terminal on an id that was never issued returns nullopt
//   immediately, even with a zero timeout.
// Boundaries: unknown id.
// On failure first check: did wait_for_terminal start synthesizing a record or
//   block for the full timeout on an unknown id?
BOOST_AUTO_TEST_CASE(wait_for_terminal_unknown_id_returns_nullopt)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    BOOST_TEST(
        !runner.wait_for_terminal("j-404", std::chrono::milliseconds(0))
             .has_value());
}

// Type: unit (wait_idle boundary / empty runner).
// Scenario: a runner with nothing submitted is already idle; wait_idle returns
//   true even with a zero timeout (one poll, no block).
// Boundaries: timeout 0, active_count == 0.
// On failure first check: did wait_idle stop treating active_count == 0 as the
//   satisfied predicate, or block despite a zero deadline?
BOOST_AUTO_TEST_CASE(wait_idle_true_on_empty_runner_timeout_zero)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    BOOST_TEST(runner.wait_idle(std::chrono::milliseconds(0)));
}

// Type: unit (wait_idle after work drains).
// Scenario: a submitted noop runs and finishes; wait_idle then returns true.
// Boundaries: active_count returns to 0 after a finish.
// On failure first check: did run_job stop signalling m_idle_cv, so wait_idle
//   only returns on timeout rather than on the finish?
BOOST_AUTO_TEST_CASE(wait_idle_true_after_jobs_finish)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    JobBody noop = [](IJobContext&) {
        JobOutcome out;
        out.ok = true;
        return out;
    };
    JobSubmitResult a = runner.submit("link.import", "aaaa", std::move(noop));
    BOOST_REQUIRE(a.job.has_value());

    BOOST_TEST(runner.wait_idle(std::chrono::seconds(10)));
}

// Type: unit (wait_idle negative while busy).
// Scenario: a gated job stays running; wait_idle returns false once its short
//   timeout elapses with work still outstanding.
// Boundaries: 50 ms timeout with one running job.
// On failure first check: did wait_idle start reporting idle while a job is
//   still running (active_count miscounted)?
BOOST_AUTO_TEST_CASE(wait_idle_false_while_gated_job_runs)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    Gate gate;
    std::promise<void> running;
    JobBody body = [&gate, &running](IJobContext&) {
        running.set_value();
        gate.wait();
        JobOutcome out;
        out.ok = true;
        return out;
    };
    JobSubmitResult a = runner.submit("link.import", "aaaa", std::move(body));
    BOOST_REQUIRE(a.job.has_value());
    running.get_future().wait();

    BOOST_TEST(!runner.wait_idle(std::chrono::milliseconds(50)));

    gate.open();
    BOOST_TEST(runner.wait_idle(std::chrono::seconds(10)));
}

// Type: unit (cancel_all over queued + running / T-88, T-90).
// Scenario: with one worker busy on a running job and three jobs queued,
//   cancel_all returns 4; the three queued jobs become cancelled without ever
//   running, and the running body observes the flag and ends cancelled with its
//   partial result preserved.
// Boundaries: 1 running + 3 queued; queued -> cancelled without running.
// On failure first check: did cancel_all stop draining the queue, miscount the
//   affected jobs, or stop flagging the running job?
BOOST_AUTO_TEST_CASE(cancel_all_cancels_queued_and_flags_running)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 8, 256});

    std::promise<void> running;
    JobSubmitResult a =
        runner.submit("link.import", "aaaa", make_cooperative(running, 2));
    BOOST_REQUIRE(a.job.has_value());
    running.get_future().wait();  // the single worker now holds job A

    std::atomic<int> queued_ran{0};
    std::vector<std::string> queued_ids;
    for (int i = 0; i < 3; ++i)
    {
        JobBody q = [&queued_ran](IJobContext&) {
            queued_ran.fetch_add(1);
            JobOutcome out;
            out.ok = true;
            return out;
        };
        JobSubmitResult r =
            runner.submit("link.import", "bbbb", std::move(q));
        BOOST_REQUIRE(r.job.has_value());
        queued_ids.push_back(r.job->id);
    }

    BOOST_TEST(runner.cancel_all() == 4u);

    // The three queued jobs are already cancelled and never ran their bodies.
    for (const std::string& id : queued_ids)
    {
        std::optional<JobRecord> rec = runner.get(id);
        BOOST_REQUIRE(rec.has_value());
        BOOST_TEST((rec->state == JobState::cancelled));
    }
    BOOST_TEST(queued_ran.load() == 0);

    // The running body saw the flag and ended cancelled, partial result kept.
    std::optional<JobRecord> fin =
        runner.wait_for_terminal(a.job->id, std::chrono::seconds(10));
    BOOST_REQUIRE(fin.has_value());
    BOOST_TEST((fin->state == JobState::cancelled));
    BOOST_REQUIRE(fin->result_json.has_value());
    BOOST_TEST(fin->result_json.value() == "{\"done\":2}");
    BOOST_TEST(queued_ran.load() == 0);
}

// Type: unit (cancel_all idempotence / T-89).
// Scenario: cancel_all affects the one running job the first time, and after it
//   drains to terminal a second cancel_all returns 0.
// Boundaries: second call with nothing active.
// On failure first check: did cancel_all start re-counting terminal jobs, or
//   leave a stale flag in m_flags?
BOOST_AUTO_TEST_CASE(cancel_all_twice_is_a_noop_the_second_time)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    std::promise<void> running;
    JobSubmitResult a =
        runner.submit("link.import", "aaaa", make_cooperative(running, 1));
    BOOST_REQUIRE(a.job.has_value());
    running.get_future().wait();

    BOOST_TEST(runner.cancel_all() == 1u);
    std::optional<JobRecord> fin =
        runner.wait_for_terminal(a.job->id, std::chrono::seconds(10));
    BOOST_REQUIRE(fin.has_value());
    BOOST_TEST((fin->state == JobState::cancelled));

    BOOST_TEST(runner.cancel_all() == 0u);
}

// Type: unit (orderly shutdown / T-88, success criteria 2 and 3).
// Scenario: shutdown joins after a cooperative body returns, leaves the runner
//   idle, ends the job cancelled, and a later submit is rejected busy.
// Boundaries: running cooperative body at shutdown; submit after shutdown.
// On failure first check: did shutdown stop calling cancel_all (so the body is
//   never told to stop), or did submit stop checking m_stopping?
BOOST_AUTO_TEST_CASE(shutdown_joins_cooperative_and_rejects_later_submit)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    std::promise<void> running;
    JobSubmitResult a =
        runner.submit("link.import", "aaaa", make_cooperative(running, 3));
    BOOST_REQUIRE(a.job.has_value());
    running.get_future().wait();

    runner.shutdown();

    // After shutdown the cooperative body has returned: the runner is idle.
    BOOST_TEST(runner.wait_idle(std::chrono::milliseconds(0)));
    std::optional<JobRecord> rec = runner.get(a.job->id);
    BOOST_REQUIRE(rec.has_value());
    BOOST_TEST((rec->state == JobState::cancelled));

    JobBody noop = [](IJobContext&) {
        JobOutcome out;
        out.ok = true;
        return out;
    };
    JobSubmitResult after =
        runner.submit("link.import", "zzzz", std::move(noop));
    BOOST_TEST(!after.job.has_value());
    BOOST_TEST((after.error.code == CommandErrorCode::busy));
}

// Type: unit (shutdown idempotence / T-89).
// Scenario: calling shutdown twice in sequence is safe -- the second call
//   returns immediately and the runner stays idle.
// Boundaries: repeated shutdown.
// On failure first check: did the second shutdown try to re-join an
//   already-joined worker (double-join) instead of returning on m_stopping?
BOOST_AUTO_TEST_CASE(shutdown_twice_is_safe)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{2, 4, 256});

    std::promise<void> running;
    JobSubmitResult a =
        runner.submit("link.import", "aaaa", make_cooperative(running, 1));
    BOOST_REQUIRE(a.job.has_value());
    running.get_future().wait();

    runner.shutdown();
    runner.shutdown();  // must not crash, hang or double-join

    BOOST_TEST(runner.wait_idle(std::chrono::milliseconds(0)));
}

// Type: unit (concurrent shutdown / T-89, TSan-style).
// Scenario: several threads call shutdown concurrently; exactly one joins the
//   workers and the rest return immediately, with no crash, deadlock or
//   double-join, leaving the runner idle and the job cancelled.
// Boundaries: concurrent shutdown callers.
// On failure first check: run under ThreadSanitizer; suspect the m_stopping
//   guard letting two callers into the join loop.
BOOST_AUTO_TEST_CASE(concurrent_shutdown_is_safe)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{2, 8, 256});

    std::promise<void> running;
    JobSubmitResult a =
        runner.submit("link.import", "aaaa", make_cooperative(running, 1));
    BOOST_REQUIRE(a.job.has_value());
    running.get_future().wait();

    std::vector<std::thread> callers;
    callers.reserve(8);
    for (int i = 0; i < 8; ++i)
    {
        callers.emplace_back([&runner] { runner.shutdown(); });
    }
    for (std::thread& t : callers)
    {
        t.join();
    }

    BOOST_TEST(runner.wait_idle(std::chrono::milliseconds(0)));
    std::optional<JobRecord> rec = runner.get(a.job->id);
    BOOST_REQUIRE(rec.has_value());
    BOOST_TEST((rec->state == JobState::cancelled));
}

// Type: unit (T-88 regression: bounded shutdown for a cooperative body).
// Scenario: a cooperative body that polls the cancel flag is released promptly
//   by shutdown, so shutdown returns well within a generous bound rather than
//   blocking the process on exit.
// Boundaries: shutdown latency for a polling body.
// On failure first check: did shutdown stop flagging the running body (so it
//   only ends when its own work finishes), reintroducing the T-88 hang?
BOOST_AUTO_TEST_CASE(shutdown_completes_within_bound_for_cooperative_body)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    std::promise<void> running;
    JobSubmitResult a =
        runner.submit("link.import", "aaaa", make_cooperative(running, 1));
    BOOST_REQUIRE(a.job.has_value());
    running.get_future().wait();

    const auto t0 = std::chrono::steady_clock::now();
    runner.shutdown();
    const auto elapsed = std::chrono::steady_clock::now() - t0;

    BOOST_TEST((elapsed < std::chrono::seconds(5)));
    BOOST_TEST(runner.wait_idle(std::chrono::milliseconds(0)));
    BOOST_TEST((runner.get(a.job->id)->state == JobState::cancelled));
}

// Type: unit (documented limitation / T-88 residual risk).
// Scenario: a body that never polls the cancel flag is NOT forcibly stopped by
//   shutdown; shutdown blocks on join until the body returns on its own, then
//   the job ends per the body's own outcome (succeeded). This documents that
//   C++ cannot kill a running thread -- a non-cooperative body still blocks
//   exit until it finishes.
// Boundaries: non-polling body + shutdown join.
// On failure first check: did shutdown start detaching or force-terminating a
//   worker instead of joining it (which would violate the no-detach rule)?
BOOST_AUTO_TEST_CASE(non_polling_body_runs_to_completion_on_shutdown)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    Gate gate;
    std::promise<void> running;
    JobBody body = [&gate, &running](IJobContext&) {
        running.set_value();
        gate.wait();  // never polls cancel_requested()
        JobOutcome out;
        out.ok = true;
        return out;
    };
    JobSubmitResult a = runner.submit("link.import", "aaaa", std::move(body));
    BOOST_REQUIRE(a.job.has_value());
    running.get_future().wait();

    std::thread stopper([&runner] { runner.shutdown(); });
    // shutdown is now blocked joining the worker running the gated body.
    gate.open();      // release the non-polling body
    stopper.join();   // shutdown returns only after the body finished

    std::optional<JobRecord> rec = runner.get(a.job->id);
    BOOST_REQUIRE(rec.has_value());
    BOOST_TEST((rec->state == JobState::succeeded));
}

// Type: unit (destructor safety with pending work / T-89).
// Scenario: the runner is destroyed with one cooperative job running and one
//   job still queued; the destructor cancels the queued job (it never runs) and
//   flags the running one, then joins without hanging or leaking.
// Boundaries: implicit shutdown via destructor with a queued job pending.
// On failure first check: did the destructor stop calling shutdown, or did
//   shutdown start draining (running) queued jobs instead of cancelling them?
BOOST_AUTO_TEST_CASE(destructor_cancels_pending_queued_without_hanging)
{
    ManualClock clock = make_clock();
    std::atomic<bool> queued_ran{false};

    {
        JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

        std::promise<void> running;
        JobSubmitResult a =
            runner.submit("link.import", "aaaa", make_cooperative(running, 1));
        BOOST_REQUIRE(a.job.has_value());
        running.get_future().wait();  // single worker is busy on job A

        JobBody q = [&queued_ran](IJobContext&) {
            queued_ran.store(true);
            JobOutcome out;
            out.ok = true;
            return out;
        };
        JobSubmitResult b =
            runner.submit("link.import", "bbbb", std::move(q));
        BOOST_REQUIRE(b.job.has_value());
        // runner goes out of scope here: destructor -> shutdown.
    }

    BOOST_TEST(!queued_ran.load());  // queued job cancelled, never run
}

// Type: unit (partial result integrity across shutdown / T-90).
// Scenario: a running cooperative job that recorded a partial result ends
//   cancelled on shutdown with that partial result_json preserved.
// Boundaries: partial result survives the cancel-on-shutdown path.
// On failure first check: did the shutdown/cancel path stop preserving the
//   body's result_json on the cancelled branch?
BOOST_AUTO_TEST_CASE(partial_result_of_cancelled_running_job_survives_shutdown)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    std::promise<void> running;
    JobSubmitResult a =
        runner.submit("link.import", "aaaa", make_cooperative(running, 7));
    BOOST_REQUIRE(a.job.has_value());
    running.get_future().wait();

    runner.shutdown();

    std::optional<JobRecord> rec = runner.get(a.job->id);
    BOOST_REQUIRE(rec.has_value());
    BOOST_TEST((rec->state == JobState::cancelled));
    BOOST_REQUIRE(rec->result_json.has_value());
    BOOST_TEST(rec->result_json.value() == "{\"done\":7}");
}
