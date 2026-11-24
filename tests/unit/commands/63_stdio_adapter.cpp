/**
 * @file 63_stdio_adapter.cpp
 * @brief Unit tests for @ref url_shortener::transport::StdioAdapter (Task 05.0
 * subtask 15): the local-operator caller, the EOF-driven job drain, the
 * cancel-on-timeout and stream-failure paths, the read-only downgrade and the
 * exit-code contract. Jobs are driven through a real @ref
 * url_shortener::commands::JobRunner with condition-variable gates, never timed
 * sleeps, so event ordering is deterministic.
 */
#define BOOST_TEST_MODULE CommandsStdioAdapterTest
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>
#include <variant>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/arg_spec.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/job_runner.hpp"
#include "url_shortener/commands/job_submitter.hpp"
#include "url_shortener/commands/job_types.hpp"
#include "url_shortener/commands/ndjson_session.hpp"
#include "url_shortener/composition/transport_factory.hpp"
#include "url_shortener/core/clock.hpp"
#include "url_shortener/transport/stdio_adapter.hpp"

using url_shortener::commands::ArgsView;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandContext;
using url_shortener::commands::CommandDescriptor;
using url_shortener::commands::CommandError;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandHandler;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandResult;
using url_shortener::commands::ExecutionKind;
using url_shortener::commands::IJobContext;
using url_shortener::commands::JobBody;
using url_shortener::commands::JobOutcome;
using url_shortener::commands::JobPage;
using url_shortener::commands::JobRunner;
using url_shortener::commands::JobRunnerOptions;
using url_shortener::commands::JobState;
using url_shortener::commands::NdjsonSession;
using url_shortener::commands::parse_response;
using url_shortener::commands::parse_response_result;
using url_shortener::commands::Permission;
using url_shortener::commands::session_end;
using url_shortener::composition::TransportOptions;
using url_shortener::transport::StdioAdapter;

namespace
{

/// A clock frozen at the epoch; the runner only reads it for timestamps.
ManualClock make_clock()
{
    return ManualClock(std::chrono::system_clock::time_point{});
}

/// A one-shot gate: a body blocks in wait() until the test calls open(). Pure
/// condition-variable synchronization, never a timed sleep.
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

/// Builds a sync descriptor with the given name, permission and handler.
CommandDescriptor sync_command(std::string name,
                               Permission permission,
                               CommandHandler handler)
{
    CommandDescriptor descriptor;
    descriptor.name = std::move(name);
    descriptor.summary = "a test command";
    descriptor.permission = permission;
    descriptor.mutating = permission != Permission::read;
    descriptor.handler = std::move(handler);
    return descriptor;
}

/// Factory of a fresh @ref JobBody per invocation, so one descriptor can run a
/// gated or cooperative body chosen by the test.
using BodyMaker = std::function<JobBody()>;

/// Registers `test.echo` (read) and `test.write` (write); when @p body_maker is
/// set, also registers a `test.job` job command whose body comes from it.
void add_commands(CommandRegistry& registry, BodyMaker body_maker = nullptr)
{
    registry.add(sync_command(
        "test.echo", Permission::read,
        [](CommandContext&, const ArgsView&) -> CommandResult
        {
            CommandResult result;
            result.result_json = R"({"ok":true})";
            return result;
        }));

    registry.add(sync_command(
        "test.write", Permission::write,
        [](CommandContext&, const ArgsView&) -> CommandResult
        {
            CommandResult result;
            result.result_json = R"({"written":true})";
            return result;
        }));

    if (body_maker) {
        CommandDescriptor job;
        job.name = "test.job";
        job.summary = "a test job command";
        job.permission = Permission::read;
        job.kind = ExecutionKind::job;
        job.job_factory =
            [body_maker](const CommandContext&,
                         const ArgsView&) -> std::variant<JobBody, CommandError>
        { return body_maker(); };
        registry.add(std::move(job));
    }
}

/// A gated body that signals it is running, blocks on @p gate, then succeeds
/// with a fixed result.
JobBody gated_body(Gate& gate, std::promise<void>& running)
{
    return [&gate, &running](IJobContext&)
    {
        running.set_value();
        gate.wait();
        JobOutcome out;
        out.ok = true;
        out.result_json = R"({"n":1})";
        return out;
    };
}

/// A cooperative body that records progress, signals it is running, spins on the
/// cancel flag, then returns cancelled with a partial result.
JobBody cooperative_body(std::promise<void>& running, std::uint64_t partial)
{
    return [&running, partial](IJobContext& ctx)
    {
        ctx.advance(partial);
        running.set_value();
        while (!ctx.cancel_requested()) {
            std::this_thread::yield();
        }
        JobOutcome out;
        out.ok = false;
        out.cancelled = true;
        out.result_json = "{\"done\":" + std::to_string(partial) + "}";
        return out;
    };
}

/// A body that advances once, signals it is running, then blocks on @p gate
/// WITHOUT ever polling its cancel flag; used to prove the EOF drain stays
/// bounded even when a body ignores cancellation (carry-forward 3).
JobBody uncooperative_body(Gate& gate, std::promise<void>& running)
{
    return [&gate, &running](IJobContext& ctx)
    {
        ctx.advance(1);
        running.set_value();
        gate.wait();  // deliberately never checks ctx.cancel_requested()
        JobOutcome out;
        out.ok = true;
        out.result_json = R"({"n":1})";
        return out;
    };
}

/// A streambuf whose every write throws a non-`std::exception` type, driving
/// StdioAdapter::run's trailing `catch (...)` (carry-forward 2). Paired with an
/// ostream whose exception mask includes `badbit`, the throw propagates out of
/// the session instead of only setting `failbit`.
class throwing_ostreambuf final : public std::streambuf
{
protected:
    int_type overflow(int_type) override { throw 42; }

    std::streamsize xsputn(const char*, std::streamsize) override
    {
        throw 42;
    }
};

/// A streambuf that fails every byte once @p max_lines newlines were emitted,
/// simulating a parent that stops reading stdout (broken pipe).
class capped_ostreambuf final : public std::streambuf
{
public:
    explicit capped_ostreambuf(std::size_t max_lines) : max_lines_(max_lines) {}

    std::string data;
    std::size_t lines = 0;

protected:
    int_type overflow(int_type ch) override
    {
        if (ch == traits_type::eof()) {
            return ch;
        }
        if (lines >= max_lines_) {
            return traits_type::eof();
        }
        const char c = traits_type::to_char_type(ch);
        data.push_back(c);
        if (c == '\n') {
            ++lines;
        }
        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override
    {
        for (std::streamsize i = 0; i < n; ++i) {
            if (overflow(traits_type::to_int_type(s[i]))
                == traits_type::eof()) {
                return i;
            }
        }
        return n;
    }

private:
    std::size_t max_lines_;
};

}  // namespace

// Type: unit (identity).
// Scenario: the adapter reports the stable transport name.
// Boundaries: name() literal.
// On failure first check: StdioAdapter::name return value.
BOOST_AUTO_TEST_CASE(name_is_stdio)
{
    std::istringstream in;
    std::ostringstream out;
    TransportOptions options;
    StdioAdapter adapter(in, out, options);

    BOOST_CHECK_EQUAL(adapter.name(), std::string_view("stdio"));
}

// Type: unit (clean EOF, no jobs).
// Scenario: a scripted session with no job runner ends at EOF and returns 0;
//   every output line is a response envelope.
// Boundaries: eof -> 0 with job_runner == nullptr (no drain).
// On failure first check: the eof branch of run and the null-runner drain skip.
BOOST_AUTO_TEST_CASE(clean_eof_no_jobs_returns_zero)
{
    CommandRegistry registry;
    add_commands(registry);

    std::istringstream in(R"({"v":1,"id":"a","command":"test.echo"})" "\n");
    std::ostringstream out;
    TransportOptions options;  // job_runner == nullptr
    StdioAdapter adapter(in, out, options);

    const int rc = adapter.run(registry);

    BOOST_TEST(rc == 0);
    const parse_response_result parsed = parse_response(out.str());
    BOOST_REQUIRE(parsed.response.has_value());
    BOOST_TEST(parsed.response->ok == true);
    BOOST_TEST(parsed.response->id == "a");
}

// Type: unit (EOF drain waits for a running job, success criterion 2).
// Scenario: a gated job is running when stdin reaches EOF; run() blocks in the
//   drain wait and only returns after the gate opens and the job succeeds.
// Boundaries: run() does not return before the gate opens (asserted); long
//   drain_timeout so the wait never times out.
// On failure first check: drain_jobs graceful wait_idle(drain_timeout), and that
//   the session returned eof (not output_failed).
BOOST_AUTO_TEST_CASE(eof_waits_for_running_job_then_succeeds)
{
    ManualClock clock = make_clock();
    auto runner =
        std::make_shared<JobRunner>(clock, JobRunnerOptions{1, 4, 256});

    Gate gate;
    std::promise<void> running;
    CommandRegistry registry;
    add_commands(registry, [&] { return gated_body(gate, running); });
    registry.set_job_submitter(runner);

    std::istringstream in(R"({"v":1,"id":"a","command":"test.job"})" "\n");
    std::ostringstream out;
    TransportOptions options;
    options.drain_timeout = std::chrono::seconds(10);
    options.job_runner = runner.get();
    StdioAdapter adapter(in, out, options);

    std::promise<int> rc_promise;
    std::future<int> rc_future = rc_promise.get_future();
    std::thread worker(
        [&] { rc_promise.set_value(adapter.run(registry)); });

    running.get_future().wait();  // the job body is running
    // run() must be blocked draining the running job: it has not returned.
    BOOST_TEST((rc_future.wait_for(std::chrono::milliseconds(100))
                == std::future_status::timeout));

    gate.open();
    worker.join();
    BOOST_TEST(rc_future.get() == 0);

    const JobPage page = runner->list(std::nullopt, 0, 10);
    BOOST_REQUIRE(page.items.size() == 1u);
    BOOST_TEST((page.items.front().state == JobState::succeeded));
}

// Type: unit (drain timeout -> cooperative cancel, success criterion 2).
// Scenario: a cooperative job is still running at EOF; the short drain timeout
//   elapses, cancel_all stops it, the job ends cancelled with its partial
//   result, and run() still returns 0.
// Boundaries: 50 ms drain_timeout; cancel_all honored; partial result kept.
// On failure first check: drain_jobs timeout path (cancel_all + grace) and the
//   eof -> 0 mapping even after a timeout.
BOOST_AUTO_TEST_CASE(drain_timeout_cancels_cooperative_job_returns_zero)
{
    ManualClock clock = make_clock();
    auto runner =
        std::make_shared<JobRunner>(clock, JobRunnerOptions{1, 4, 256});

    std::promise<void> running;
    CommandRegistry registry;
    add_commands(registry, [&] { return cooperative_body(running, 3); });
    registry.set_job_submitter(runner);

    std::istringstream in(R"({"v":1,"id":"a","command":"test.job"})" "\n");
    std::ostringstream out;
    TransportOptions options;
    options.drain_timeout = std::chrono::milliseconds(50);
    options.job_runner = runner.get();
    StdioAdapter adapter(in, out, options);

    std::promise<int> rc_promise;
    std::future<int> rc_future = rc_promise.get_future();
    std::thread worker(
        [&] { rc_promise.set_value(adapter.run(registry)); });

    running.get_future().wait();  // ensure the body is running before EOF drains
    worker.join();

    BOOST_TEST(rc_future.get() == 0);
    const JobPage page = runner->list(std::nullopt, 0, 10);
    BOOST_REQUIRE(page.items.size() == 1u);
    BOOST_TEST((page.items.front().state == JobState::cancelled));
    BOOST_REQUIRE(page.items.front().result_json.has_value());
    BOOST_TEST(page.items.front().result_json.value() == R"({"done":3})");
}

// Type: unit (output stream failure -> 4, running job cancelled, T-102/T-106).
// Scenario: the output stream fails on the first write, so the job envelope
//   cannot be delivered; run() returns 4 and the already-submitted cooperative
//   job is cancelled during the failure drain.
// Boundaries: output_failed -> 4; cancel_all on the failure path.
// On failure first check: drain_jobs(graceful=false) cancel-immediately path and
//   the output_failed -> 4 mapping.
BOOST_AUTO_TEST_CASE(output_failure_cancels_job_returns_four)
{
    ManualClock clock = make_clock();
    auto runner =
        std::make_shared<JobRunner>(clock, JobRunnerOptions{1, 4, 256});

    std::promise<void> running;
    CommandRegistry registry;
    add_commands(registry, [&] { return cooperative_body(running, 5); });
    registry.set_job_submitter(runner);

    std::istringstream in(R"({"v":1,"id":"a","command":"test.job"})" "\n");
    capped_ostreambuf buf(0);  // refuse the very first byte
    std::ostream out(&buf);
    TransportOptions options;
    options.job_runner = runner.get();
    StdioAdapter adapter(in, out, options);

    const int rc = adapter.run(registry);

    BOOST_TEST(rc == 4);
    const JobPage page = runner->list(std::nullopt, 0, 10);
    BOOST_REQUIRE(page.items.size() == 1u);
    BOOST_TEST((page.items.front().state == JobState::cancelled));
}

// Type: unit (invalid input: hard read error -> 4).
// Scenario: an input stream already in badbit yields input_failed; run() returns
//   4 and writes nothing.
// Boundaries: input_failed -> 4 with an idle runner (drain is a no-op).
// On failure first check: the input_failed -> 4 mapping.
BOOST_AUTO_TEST_CASE(badbit_input_returns_four)
{
    ManualClock clock = make_clock();
    auto runner =
        std::make_shared<JobRunner>(clock, JobRunnerOptions{1, 4, 256});

    CommandRegistry registry;
    add_commands(registry);

    std::istringstream in(R"({"v":1,"id":"a","command":"test.echo"})" "\n");
    in.setstate(std::ios::badbit);
    std::ostringstream out;
    TransportOptions options;
    options.job_runner = runner.get();
    StdioAdapter adapter(in, out, options);

    const int rc = adapter.run(registry);

    BOOST_TEST(rc == 4);
    BOOST_TEST(out.str().empty());
}

// Type: unit (read-only downgrade, T-107/C7).
// Scenario: with read_only set, the caller is User: a write command is
//   not_authorized while a read command succeeds. The role comes from options,
//   not the stream.
// Boundaries: Admin -> User downgrade; authorization stays in execute.
// On failure first check: the control_set selection in run (read_only ? User :
//   Admin).
BOOST_AUTO_TEST_CASE(read_only_denies_write_allows_read)
{
    CommandRegistry registry;
    add_commands(registry);

    std::istringstream in(
        R"({"v":1,"id":"w","command":"test.write"})" "\n"
        R"({"v":1,"id":"r","command":"test.echo"})" "\n");
    std::ostringstream out;
    TransportOptions options;
    options.read_only = true;
    StdioAdapter adapter(in, out, options);

    const int rc = adapter.run(registry);
    BOOST_TEST(rc == 0);

    std::istringstream split(out.str());
    std::string first;
    std::string second;
    std::getline(split, first);
    std::getline(split, second);
    BOOST_TEST((parse_response(first).response->error.code
                == CommandErrorCode::not_authorized));
    BOOST_TEST(parse_response(second).response->ok == true);
}

// Type: unit (admin default).
// Scenario: with read_only unset the caller is Admin and a write command
//   succeeds.
// Boundaries: default role is Admin (full local-operator rights).
// On failure first check: the control_set default branch in run.
BOOST_AUTO_TEST_CASE(admin_default_allows_write)
{
    CommandRegistry registry;
    add_commands(registry);

    std::istringstream in(R"({"v":1,"id":"w","command":"test.write"})" "\n");
    std::ostringstream out;
    TransportOptions options;  // read_only == false
    StdioAdapter adapter(in, out, options);

    const int rc = adapter.run(registry);

    BOOST_TEST(rc == 0);
    BOOST_TEST(parse_response(out.str()).response->ok == true);
}

// Type: unit (no runner wired: no drain).
// Scenario: with job_runner == nullptr the adapter performs no drain and returns
//   0 at EOF.
// Boundaries: the null-runner guard in drain_jobs.
// On failure first check: the job_runner == nullptr early return.
BOOST_AUTO_TEST_CASE(null_runner_no_drain_returns_zero)
{
    CommandRegistry registry;
    add_commands(registry);

    std::istringstream in(R"({"v":1,"id":"a","command":"test.echo"})" "\n");
    std::ostringstream out;
    TransportOptions options;
    options.job_runner = nullptr;
    StdioAdapter adapter(in, out, options);

    BOOST_TEST(adapter.run(registry) == 0);
}

// Type: unit (wire purity, T-102/C2).
// Scenario: the adapter writes exactly the response envelopes the session would
//   produce for the same scripted input and the same local-operator caller, and
//   nothing else (no banner, prompt or log on stdout).
// Boundaries: full-output equality against a bare NdjsonSession.
// On failure first check: whether run writes any byte beyond NdjsonSession's
//   output (a banner/log leak) or uses a different caller.
BOOST_AUTO_TEST_CASE(output_is_only_envelope_lines)
{
    const std::string input =
        R"({"v":1,"id":"a","command":"test.echo"})" "\n"
        R"({"v":1,"id":"b","command":"test.write"})" "\n"
        R"({"v":1,"id":"c","command":"test.echo"})" "\n";

    // Expected: a bare session over the same input with the exact caller the
    // adapter builds (local-operator / stdio / Admin).
    CommandRegistry expected_registry;
    add_commands(expected_registry);
    CallerContext caller;
    caller.principal = "local-operator";
    caller.transport = "stdio";
    caller.control_set = ControlSet::Admin;
    std::istringstream expected_in(input);
    std::ostringstream expected_out;
    NdjsonSession session(expected_in, expected_out, 65536);
    session.serve(expected_registry, caller);

    // Actual: the adapter over the same input.
    CommandRegistry registry;
    add_commands(registry);
    std::istringstream in(input);
    std::ostringstream out;
    TransportOptions options;
    StdioAdapter adapter(in, out, options);
    const int rc = adapter.run(registry);

    BOOST_TEST(rc == 0);
    BOOST_CHECK_EQUAL(out.str(), expected_out.str());
}

// Type: unit (bounded shutdown on a non-cooperative body, carry-forward 3 / T-88).
// Scenario: a job whose body ignores its cancel flag and blocks on a latch is
//   running at EOF. The short drain_timeout elapses, cancel_all has no effect,
//   the 5 s grace elapses, and run() still RETURNS (skipping the blocking join)
//   rather than hanging forever. Only after asserting that bound does the test
//   release the latch, so the body's termination cannot be what unblocked run().
// Boundaries: 50 ms drain_timeout + 5 s grace; run() returns with the body still
//   wedged; the skipped join is completed by a later shutdown().
// On failure first check: drain_jobs' idle check — a non-idle runner must skip
//   runner.shutdown() (the unconditional join) and warn stdio_drain_abandoned.
BOOST_AUTO_TEST_CASE(hung_body_drain_is_bounded_returns_zero)
{
    ManualClock clock = make_clock();
    auto runner =
        std::make_shared<JobRunner>(clock, JobRunnerOptions{1, 4, 256});

    Gate gate;
    std::promise<void> running;
    CommandRegistry registry;
    add_commands(registry, [&] { return uncooperative_body(gate, running); });
    registry.set_job_submitter(runner);

    std::istringstream in(R"({"v":1,"id":"a","command":"test.job"})" "\n");
    std::ostringstream out;
    TransportOptions options;
    options.drain_timeout = std::chrono::milliseconds(50);
    options.job_runner = runner.get();
    StdioAdapter adapter(in, out, options);

    std::promise<int> rc_promise;
    std::future<int> rc_future = rc_promise.get_future();
    std::thread worker(
        [&] { rc_promise.set_value(adapter.run(registry)); });

    running.get_future().wait();  // the hung body is running, blocked on the gate
    // The bound: run() returns after drain_timeout + the 5 s cancel grace even
    // though the body never observes its cancel flag. A generous 60 s ceiling
    // distinguishes "bounded" from "hung forever".
    const std::future_status status =
        rc_future.wait_for(std::chrono::seconds(60));
    BOOST_REQUIRE((status == std::future_status::ready));
    BOOST_TEST(rc_future.get() == 0);
    worker.join();

    // Release the wedged body only now, so its completion cannot have been what
    // unblocked run(); the skipped join is then completed deterministically.
    gate.open();
    runner->shutdown();
}

// Type: unit (a non-std exception escaping the session is caught, carry-forward 2).
// Scenario: the output stream's buffer throws a non-std exception type (int) on
//   the first write, and its exception mask makes that throw propagate out of the
//   session. run() must catch it in the trailing catch(...), not reach
//   std::terminate, and map to exit 4.
// Boundaries: catch(...) (not catch(std::exception&)); output exit 4.
// On failure first check: the trailing catch(...) in StdioAdapter::run and that
//   the ostream exception mask rethrows the original non-std exception.
BOOST_AUTO_TEST_CASE(non_std_exception_is_caught_returns_four)
{
    CommandRegistry registry;
    add_commands(registry);

    std::istringstream in(R"({"v":1,"id":"a","command":"test.echo"})" "\n");
    throwing_ostreambuf buf;
    std::ostream out(&buf);
    out.exceptions(std::ios::badbit);  // make the non-std throw propagate

    TransportOptions options;  // no job runner
    StdioAdapter adapter(in, out, options);

    BOOST_TEST(adapter.run(registry) == 4);
}
