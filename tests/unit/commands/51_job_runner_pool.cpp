#define BOOST_TEST_MODULE CommandsJobRunnerPoolTest
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <stdexcept>
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
using url_shortener::commands::make_error;

namespace
{

// A clock frozen at the epoch. The runner reads it only for record timestamps
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

// Spins (yielding) until @p pred holds for the job's record, then returns that
// record. Not a sleep-based wait: it returns the instant the predicate is true
// and fails (rather than hanging) if the deadline passes, so a regression shows
// up as a failure instead of a frozen run.
JobRecord wait_until(const JobRunner& runner, const std::string& id,
                     const std::function<bool(const JobRecord&)>& pred)
{
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;)
    {
        std::optional<JobRecord> record = runner.get(id);
        BOOST_REQUIRE(record.has_value());
        if (pred(*record))
        {
            return *record;
        }
        BOOST_REQUIRE(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}

JobRecord wait_terminal(const JobRunner& runner, const std::string& id)
{
    return wait_until(runner, id,
                      [](const JobRecord& r) { return is_terminal(r.state); });
}

}  // namespace

// Type: unit (worker pool happy path).
// Scenario: a submitted body reports progress and returns ok; the job ends
//   succeeded with the body's result_json and final progress visible via get().
// Boundaries: single worker; one job start-to-finish.
// On failure first check: did run_job stop mapping outcome.ok to succeeded, or
//   stop forwarding result_json / progress to the store?
BOOST_AUTO_TEST_CASE(submit_runs_body_and_succeeds_with_result_and_progress)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    JobBody body = [](IJobContext& ctx) {
        ctx.set_total(3);
        ctx.advance(3);
        JobOutcome out;
        out.ok = true;
        out.result_json = "{\"n\":3}";
        return out;
    };

    JobSubmitResult submitted =
        runner.submit("link.import", "0123456789abcdef", std::move(body));
    BOOST_REQUIRE(submitted.job.has_value());
    BOOST_TEST((submitted.job->state == JobState::queued));

    JobRecord final_record = wait_terminal(runner, submitted.job->id);
    BOOST_TEST((final_record.state == JobState::succeeded));
    BOOST_REQUIRE(final_record.result_json.has_value());
    BOOST_TEST(final_record.result_json.value() == "{\"n\":3}");
    BOOST_TEST(final_record.progress.done == 3u);
    BOOST_TEST(final_record.progress.total == 3u);
    BOOST_TEST(!final_record.failure.has_value());
}

// Type: unit (expected failure mapping).
// Scenario: a body returning ok == false with a CommandError ends failed with
//   that code converted into the record's JobFailure.
// Boundaries: failure code carried through from body to record.
// On failure first check: did run_job stop converting outcome.failure, or map
//   the code to the wrong error name?
BOOST_AUTO_TEST_CASE(failed_outcome_ends_failed_with_the_code)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    JobBody body = [](IJobContext&) {
        JobOutcome out;
        out.ok = false;
        out.failure = make_error(CommandErrorCode::invalid_field, "bad row");
        return out;
    };

    JobSubmitResult submitted =
        runner.submit("link.import", "deadbeefdeadbeef", std::move(body));
    BOOST_REQUIRE(submitted.job.has_value());

    JobRecord final_record = wait_terminal(runner, submitted.job->id);
    BOOST_TEST((final_record.state == JobState::failed));
    BOOST_REQUIRE(final_record.failure.has_value());
    BOOST_TEST(final_record.failure->code == "invalid_field");
}

// Type: unit (exception containment / C6).
// Scenario: a body that throws ends failed with the fixed internal code and the
//   fixed "job failed" message; the exception's text never reaches the record.
// Boundaries: std::exception path; message must not leak what().
// On failure first check: did the catch block start copying ex.what() into the
//   stored failure message instead of logging it only?
BOOST_AUTO_TEST_CASE(thrown_exception_ends_failed_internal_without_leaking_text)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    const std::string leaky_detail = "needle-path-/etc/shadow";
    JobBody body = [leaky_detail](IJobContext&) -> JobOutcome {
        throw std::runtime_error(leaky_detail);
    };

    JobSubmitResult submitted =
        runner.submit("link.export", "feedfacefeedface", std::move(body));
    BOOST_REQUIRE(submitted.job.has_value());

    JobRecord final_record = wait_terminal(runner, submitted.job->id);
    BOOST_TEST((final_record.state == JobState::failed));
    BOOST_REQUIRE(final_record.failure.has_value());
    BOOST_TEST(final_record.failure->code == "internal");
    BOOST_TEST(final_record.failure->message == "job failed");
    BOOST_TEST(final_record.failure->message.find("needle-path") ==
               std::string::npos);
}

// Type: unit (bounded queue / T-86).
// Scenario: with one worker and a two-deep queue, a gated running job plus two
//   queued jobs fill capacity; the fourth submit returns busy and creates no
//   record (the store count stays three).
// Boundaries: queue.size() == queue_capacity is the reject edge.
// On failure first check: did submit start accepting past capacity, or create a
//   record before the capacity check?
BOOST_AUTO_TEST_CASE(full_queue_returns_busy_and_creates_no_record)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 2, 256});

    Gate gate;
    std::promise<void> running;
    JobBody gated = [&gate, &running](IJobContext&) {
        running.set_value();
        gate.wait();
        JobOutcome out;
        out.ok = true;
        return out;
    };

    JobSubmitResult a = runner.submit("link.import", "aaaa", std::move(gated));
    BOOST_REQUIRE(a.job.has_value());
    running.get_future().wait();  // the single worker now holds job A in-body

    JobBody noop = [](IJobContext&) {
        JobOutcome out;
        out.ok = true;
        return out;
    };
    JobSubmitResult b = runner.submit("link.import", "bbbb", JobBody(noop));
    JobSubmitResult c = runner.submit("link.import", "cccc", JobBody(noop));
    BOOST_REQUIRE(b.job.has_value());
    BOOST_REQUIRE(c.job.has_value());

    BOOST_TEST(runner.list(std::nullopt, 0, 100).total == 3u);

    JobSubmitResult d = runner.submit("link.import", "dddd", JobBody(noop));
    BOOST_TEST(!d.job.has_value());
    BOOST_TEST((d.error.code == CommandErrorCode::busy));
    BOOST_TEST(runner.list(std::nullopt, 0, 100).total == 3u);

    gate.open();
    wait_terminal(runner, a.job->id);
}

// Type: unit (cancel of a queued job / T-90).
// Scenario: with one worker busy on a gated job, a second queued job is
//   cancelled; it ends cancelled and its body is never invoked.
// Boundaries: queued -> cancelled transition without running.
// On failure first check: did cancel stop erasing the queued job, or start
//   leaving it to run?
BOOST_AUTO_TEST_CASE(cancel_queued_job_never_runs_body)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    Gate gate;
    std::promise<void> running;
    JobBody gated = [&gate, &running](IJobContext&) {
        running.set_value();
        gate.wait();
        JobOutcome out;
        out.ok = true;
        return out;
    };
    JobSubmitResult a = runner.submit("link.import", "aaaa", std::move(gated));
    BOOST_REQUIRE(a.job.has_value());
    running.get_future().wait();

    std::atomic<bool> invoked{false};
    JobBody queued = [&invoked](IJobContext&) {
        invoked.store(true);
        JobOutcome out;
        out.ok = true;
        return out;
    };
    JobSubmitResult b = runner.submit("link.import", "bbbb", std::move(queued));
    BOOST_REQUIRE(b.job.has_value());

    std::optional<JobRecord> cancelled = runner.cancel(b.job->id);
    BOOST_REQUIRE(cancelled.has_value());
    BOOST_TEST((cancelled->state == JobState::cancelled));
    BOOST_TEST(!invoked.load());

    gate.open();
    wait_terminal(runner, a.job->id);
    // Even after the worker is free, the cancelled body is never run.
    BOOST_TEST(!invoked.load());
}

// Type: unit (cooperative cancel of a running job / T-90).
// Scenario: a running body observes cancel_requested(), then returns
//   cancelled == true with a partial result; the record ends cancelled keeping
//   that partial result, and cancel() returned the still-running record.
// Boundaries: running -> flag set -> body-driven cancelled; partial kept.
// On failure first check: did cancel stop setting the flag, or did run_job stop
//   preserving result_json on the cancelled branch?
BOOST_AUTO_TEST_CASE(cancel_running_job_is_cooperative_and_keeps_partial)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    Gate finish_gate;
    std::promise<void> running;
    std::atomic<bool> observed_cancel{false};
    JobBody body = [&finish_gate, &running,
                    &observed_cancel](IJobContext& ctx) {
        ctx.advance(5);
        running.set_value();
        while (!ctx.cancel_requested())
        {
            std::this_thread::yield();
        }
        observed_cancel.store(true);
        finish_gate.wait();  // hold the body so cancel() observes running
        JobOutcome out;
        out.ok = false;
        out.cancelled = true;
        out.result_json = "{\"done\":5}";
        return out;
    };

    JobSubmitResult a = runner.submit("link.import", "aaaa", std::move(body));
    BOOST_REQUIRE(a.job.has_value());
    running.get_future().wait();

    std::optional<JobRecord> during = runner.cancel(a.job->id);
    BOOST_REQUIRE(during.has_value());
    BOOST_TEST((during->state == JobState::running));

    finish_gate.open();
    JobRecord final_record = wait_terminal(runner, a.job->id);
    BOOST_TEST(observed_cancel.load());
    BOOST_TEST((final_record.state == JobState::cancelled));
    BOOST_REQUIRE(final_record.result_json.has_value());
    BOOST_TEST(final_record.result_json.value() == "{\"done\":5}");
}

// Type: unit (cancel of a terminal job / T-90).
// Scenario: cancelling an already-succeeded job is a no-op that returns the
//   final record unchanged.
// Boundaries: terminal -> no transition.
// On failure first check: did cancel start mutating a terminal record instead
//   of returning it untouched?
BOOST_AUTO_TEST_CASE(cancel_finished_job_is_a_noop)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    JobBody body = [](IJobContext&) {
        JobOutcome out;
        out.ok = true;
        out.result_json = "{\"ok\":1}";
        return out;
    };
    JobSubmitResult a = runner.submit("link.import", "aaaa", std::move(body));
    BOOST_REQUIRE(a.job.has_value());
    wait_terminal(runner, a.job->id);

    std::optional<JobRecord> cancelled = runner.cancel(a.job->id);
    BOOST_REQUIRE(cancelled.has_value());
    BOOST_TEST((cancelled->state == JobState::succeeded));
    BOOST_REQUIRE(cancelled->result_json.has_value());
    BOOST_TEST(cancelled->result_json.value() == "{\"ok\":1}");
}

// Type: unit (invalid input).
// Scenario: cancelling an id that was never issued returns nullopt.
// Boundaries: unknown id.
// On failure first check: did cancel start synthesizing a record for an unknown
//   id?
BOOST_AUTO_TEST_CASE(cancel_unknown_id_returns_nullopt)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    BOOST_TEST(!runner.cancel("j-999").has_value());
}

// Type: unit (cooperative cancel is advisory / T-90).
// Scenario: a body that ignores a set cancel flag runs to completion and ends
//   succeeded, proving cancel never forcibly kills a body.
// Boundaries: flag set, body returns ok == true.
// On failure first check: did run_job start forcing a cancelled state when the
//   flag is set rather than honoring the body's own outcome?
BOOST_AUTO_TEST_CASE(flag_ignored_body_runs_to_completion)
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
        out.result_json = "{\"ok\":1}";
        return out;
    };
    JobSubmitResult a = runner.submit("link.import", "aaaa", std::move(body));
    BOOST_REQUIRE(a.job.has_value());
    running.get_future().wait();

    std::optional<JobRecord> during = runner.cancel(a.job->id);
    BOOST_REQUIRE(during.has_value());
    BOOST_TEST((during->state == JobState::running));

    gate.open();
    JobRecord final_record = wait_terminal(runner, a.job->id);
    BOOST_TEST((final_record.state == JobState::succeeded));
}

// Type: unit (parallelism / worker_count).
// Scenario: with two workers, two gated jobs both reach running at the same
//   time; both are observed running before either is released.
// Boundaries: worker_count == 2 runs two bodies concurrently.
// On failure first check: did the pool start serializing jobs (e.g. one worker
//   thread spawned, or mark_running moved under a shared lock)?
BOOST_AUTO_TEST_CASE(two_workers_run_two_jobs_in_parallel)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{2, 4, 256});

    Gate gate;
    std::promise<void> running_a;
    std::promise<void> running_b;

    auto make_body = [&gate](std::promise<void>& started) {
        return [&gate, &started](IJobContext&) {
            started.set_value();
            gate.wait();
            JobOutcome out;
            out.ok = true;
            return out;
        };
    };

    JobSubmitResult a =
        runner.submit("link.import", "aaaa", make_body(running_a));
    JobSubmitResult b =
        runner.submit("link.import", "bbbb", make_body(running_b));
    BOOST_REQUIRE(a.job.has_value());
    BOOST_REQUIRE(b.job.has_value());

    running_a.get_future().wait();
    running_b.get_future().wait();

    BOOST_TEST((runner.get(a.job->id)->state == JobState::running));
    BOOST_TEST((runner.get(b.job->id)->state == JobState::running));

    gate.open();
    wait_terminal(runner, a.job->id);
    wait_terminal(runner, b.job->id);
}

// Type: unit (progress visibility).
// Scenario: while a job is running, set_total/advance are visible through get()
//   before the body returns.
// Boundaries: in-flight progress read.
// On failure first check: did JobContext stop writing through
//   JobStore::update_progress, or did get() stop reflecting a running record?
BOOST_AUTO_TEST_CASE(progress_is_visible_through_get_while_running)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{1, 4, 256});

    Gate gate;
    std::promise<void> running;
    JobBody body = [&gate, &running](IJobContext& ctx) {
        ctx.set_total(10);
        ctx.advance(4);
        running.set_value();
        gate.wait();
        JobOutcome out;
        out.ok = true;
        return out;
    };
    JobSubmitResult a = runner.submit("link.import", "aaaa", std::move(body));
    BOOST_REQUIRE(a.job.has_value());
    running.get_future().wait();

    std::optional<JobRecord> mid = runner.get(a.job->id);
    BOOST_REQUIRE(mid.has_value());
    BOOST_TEST((mid->state == JobState::running));
    BOOST_TEST(mid->progress.total == 10u);
    BOOST_TEST(mid->progress.done == 4u);

    gate.open();
    wait_terminal(runner, a.job->id);
}

// Type: unit (options validation / T-86).
// Scenario: constructing with any zero bound throws std::invalid_argument and
//   starts no threads.
// Boundaries: worker_count/queue_capacity/max_retained_jobs == 0.
// On failure first check: did validate_options stop running before member
//   construction, or stop checking one of the three bounds?
BOOST_AUTO_TEST_CASE(options_validation_rejects_zero_bounds)
{
    ManualClock clock = make_clock();
    BOOST_CHECK_THROW(JobRunner(clock, JobRunnerOptions{0, 4, 256}),
                      std::invalid_argument);
    BOOST_CHECK_THROW(JobRunner(clock, JobRunnerOptions{1, 0, 256}),
                      std::invalid_argument);
    BOOST_CHECK_THROW(JobRunner(clock, JobRunnerOptions{1, 4, 0}),
                      std::invalid_argument);
}

// Type: unit (stress / submit-cancel race, T-89/T-90).
// Scenario: repeatedly submit jobs while a second thread cancels the newest id;
//   every accepted job ends terminal, and the run completes without deadlock,
//   data race or crash.
// Boundaries: concurrent submit vs cancel vs worker finish over many rounds.
// On failure first check: run under ThreadSanitizer; suspect the m_mutex/store
//   lock order (T-89) or the flag erase race in run_job.
BOOST_AUTO_TEST_CASE(submit_cancel_race_stress)
{
    ManualClock clock = make_clock();
    JobRunner runner(clock, JobRunnerOptions{4, 64, 100000});

    constexpr int k_jobs = 400;
    std::vector<std::string> ids;
    ids.reserve(k_jobs);
    std::mutex ids_mutex;
    std::atomic<bool> submitting{true};

    std::thread canceller([&] {
        // Runs only while the submitter is active; it stops as soon as the
        // submit loop clears the flag, so join() always returns.
        while (submitting.load())
        {
            std::string pick;
            {
                std::lock_guard<std::mutex> lock(ids_mutex);
                if (!ids.empty())
                {
                    pick = ids.back();
                }
            }
            if (!pick.empty())
            {
                runner.cancel(pick);
            }
            std::this_thread::yield();
        }
    });

    for (int i = 0; i < k_jobs; ++i)
    {
        JobBody body = [](IJobContext& ctx) {
            for (int step = 0; step < 4; ++step)
            {
                if (ctx.cancel_requested())
                {
                    JobOutcome cancelled;
                    cancelled.ok = false;
                    cancelled.cancelled = true;
                    return cancelled;
                }
                ctx.advance(1);
            }
            JobOutcome out;
            out.ok = true;
            return out;
        };
        JobSubmitResult submitted =
            runner.submit("link.import", "digestdigestdige", std::move(body));
        if (submitted.job.has_value())
        {
            std::lock_guard<std::mutex> lock(ids_mutex);
            ids.push_back(submitted.job->id);
        }
        // Busy results (queue full) are expected under load and simply dropped.
    }
    submitting.store(false);
    canceller.join();

    std::vector<std::string> accepted;
    {
        std::lock_guard<std::mutex> lock(ids_mutex);
        accepted = ids;
    }
    for (const std::string& id : accepted)
    {
        JobRecord record = wait_terminal(runner, id);
        BOOST_TEST(is_terminal(record.state));
    }
}
