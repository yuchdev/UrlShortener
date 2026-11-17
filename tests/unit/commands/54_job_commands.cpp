#define BOOST_TEST_MODULE CommandsJobCommandsTest
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/builtin_commands.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/job_commands.hpp"
#include "url_shortener/commands/job_runner.hpp"
#include "url_shortener/commands/job_submitter.hpp"
#include "url_shortener/commands/job_types.hpp"
#include "url_shortener/core/clock.hpp"

using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandDescriptor;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::IJobContext;
using url_shortener::commands::JobBody;
using url_shortener::commands::JobFailure;
using url_shortener::commands::JobOutcome;
using url_shortener::commands::JobProgress;
using url_shortener::commands::JobRecord;
using url_shortener::commands::JobRunner;
using url_shortener::commands::JobRunnerOptions;
using url_shortener::commands::JobState;
using url_shortener::commands::JobSubmitResult;
using url_shortener::commands::Permission;
using url_shortener::commands::register_builtin_commands;
using url_shortener::commands::register_job_commands;
using url_shortener::commands::serialize_job_json;

namespace
{

using namespace std::chrono_literals;

// A clock frozen at the epoch; the runner reads it only for record timestamps
// and never mutates it, so concurrent worker reads are race-free.
ManualClock make_clock()
{
    return ManualClock(std::chrono::system_clock::time_point {});
}

// A caller with a recognizable principal/transport and the given role.
CallerContext caller_with(ControlSet role)
{
    CallerContext caller;
    caller.principal = "local-operator";
    caller.transport = "test";
    caller.control_set = role;
    return caller;
}

// A request for `command` carrying the given args JSON text.
CommandRequest request_for(std::string command, std::string args_json = "{}")
{
    CommandRequest request;
    request.id = "client-1";
    request.command = std::move(command);
    request.args_json = std::move(args_json);
    return request;
}

// A one-shot gate plus a cancel-observation cell, all shared so a body captures
// only shared ownership and plain values (threat model T-87).
struct Gate
{
    std::shared_ptr<std::promise<void>> open =
        std::make_shared<std::promise<void>>();
    std::shared_ptr<std::atomic<bool>> saw_cancel =
        std::make_shared<std::atomic<bool>>(false);
};

// Submits a gated body that blocks until `gate` is opened while cooperatively
// polling cancel; on cancel it returns a partial result, otherwise a success
// result. Returns the accepted job id.
std::string submit_gated(JobRunner& runner, Gate gate,
                         std::string command = "link.import")
{
    const std::shared_future<void> opened = gate.open->get_future().share();
    JobSubmitResult result = runner.submit(
        std::move(command), "0123456789abcdef",
        [gate, opened](IJobContext& ctx) -> JobOutcome
        {
            for (;;) {
                if (ctx.cancel_requested()) {
                    gate.saw_cancel->store(true);
                    JobOutcome out;
                    out.ok = false;
                    out.cancelled = true;
                    out.result_json = R"({"partial":1})";
                    return out;
                }
                if (opened.wait_for(0ms) == std::future_status::ready) {
                    break;
                }
                std::this_thread::yield();
            }
            JobOutcome out;
            out.ok = true;
            out.result_json = R"({"done":1})";
            return out;
        });
    BOOST_REQUIRE(result.job.has_value());
    return result.job->id;
}

// Spins (never sleeps) until the job is observed `running`, bounded by a
// generous ceiling so a regression fails rather than hangs.
bool wait_running(const JobRunner& runner, const std::string& id)
{
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline) {
        const std::optional<JobRecord> record = runner.get(id);
        if (record.has_value() && record->state == JobState::running) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

// Builds a hand-made record at the epoch so serialize_job_json can be tested
// deterministically without the thread pool.
JobRecord make_record(JobState state)
{
    JobRecord record;
    record.id = "j-7";
    record.command = "link.import";
    record.args_digest = "deadbeefdeadbeef";  // a marker: must never be emitted
    record.state = state;
    record.progress = JobProgress {3, 10};
    record.created_at = std::chrono::system_clock::time_point {};
    return record;
}

}  // namespace

// Type: unit (serializer shape, queued / C2).
// Scenario: a queued record serializes with id/command/state/progress/
//   created_at and omits result, error, started_at and finished_at.
// Boundaries: no optional field appears before the job has started.
// On failure first check: did serialize_job_json start emitting started_at or
//   finished_at for a non-terminal, never-run job?
BOOST_AUTO_TEST_CASE(serialize_queued_omits_optional_fields)
{
    const std::string json = serialize_job_json(make_record(JobState::queued));

    BOOST_TEST(json.find(R"("id":"j-7")") != std::string::npos);
    BOOST_TEST(json.find(R"("command":"link.import")") != std::string::npos);
    BOOST_TEST(json.find(R"("state":"queued")") != std::string::npos);
    BOOST_TEST(json.find(R"("progress":{"done":3,"total":10})")
               != std::string::npos);
    BOOST_TEST(json.find(R"("created_at":)") != std::string::npos);
    BOOST_TEST(json.find(R"("result":)") == std::string::npos);
    BOOST_TEST(json.find(R"("error":)") == std::string::npos);
    BOOST_TEST(json.find(R"("started_at":)") == std::string::npos);
    BOOST_TEST(json.find(R"("finished_at":)") == std::string::npos);
    // The args digest is never serialized (contract C6, threat model T-93).
    BOOST_TEST(json.find("deadbeefdeadbeef") == std::string::npos);
}

// Type: unit (serializer shape, running).
// Scenario: a running record has started_at but no finished_at, no error and
//   no result (none set).
// Boundaries: started_at present once the job has left queued.
// On failure first check: did the serializer stop gating started_at on the
//   optional having a value?
BOOST_AUTO_TEST_CASE(serialize_running_has_started_at_only)
{
    JobRecord record = make_record(JobState::running);
    record.started_at = std::chrono::system_clock::time_point {} + 1s;
    const std::string json = serialize_job_json(record);

    BOOST_TEST(json.find(R"("state":"running")") != std::string::npos);
    BOOST_TEST(json.find(R"("started_at":)") != std::string::npos);
    BOOST_TEST(json.find(R"("finished_at":)") == std::string::npos);
    BOOST_TEST(json.find(R"("error":)") == std::string::npos);
    BOOST_TEST(json.find(R"("result":)") == std::string::npos);
}

// Type: unit (serializer shape, succeeded; result embedded as object).
// Scenario: a succeeded record with a nested result_json embeds it verbatim as
//   an object (valid nesting, not escaped), with started_at and finished_at and
//   no error.
// Boundaries: result is opaque pre-serialized text embedded unchanged (C9).
// On failure first check: did the serializer start escaping result_json or
//   wrapping it in a string instead of embedding it as an object?
BOOST_AUTO_TEST_CASE(serialize_succeeded_embeds_result_object_verbatim)
{
    JobRecord record = make_record(JobState::succeeded);
    record.started_at = std::chrono::system_clock::time_point {} + 1s;
    record.finished_at = std::chrono::system_clock::time_point {} + 2s;
    record.result_json = R"({"a":{"b":[1,2]},"created":5})";
    const std::string json = serialize_job_json(record);

    BOOST_TEST(json.find(R"("state":"succeeded")") != std::string::npos);
    // Embedded unescaped as a nested object (valid nesting).
    BOOST_TEST(json.find(R"("result":{"a":{"b":[1,2]},"created":5})")
               != std::string::npos);
    BOOST_TEST(json.find(R"("started_at":)") != std::string::npos);
    BOOST_TEST(json.find(R"("finished_at":)") != std::string::npos);
    BOOST_TEST(json.find(R"("error":)") == std::string::npos);
}

// Type: unit (serializer shape, failed).
// Scenario: a failed record emits error{code,message} from its stored failure
//   and a finished_at, and never emits a cancelled code.
// Boundaries: error present for a failed job, carrying the stored detail.
// On failure first check: did the serializer stop reading record.failure for a
//   failed job?
BOOST_AUTO_TEST_CASE(serialize_failed_emits_stored_error)
{
    JobRecord record = make_record(JobState::failed);
    record.finished_at = std::chrono::system_clock::time_point {} + 2s;
    record.failure = JobFailure {"invalid_field", "row 3 rejected"};
    const std::string json = serialize_job_json(record);

    BOOST_TEST(json.find(R"("state":"failed")") != std::string::npos);
    BOOST_TEST(
        json.find(R"("error":{"code":"invalid_field","message":"row 3 rejected"})")
        != std::string::npos);
    BOOST_TEST(json.find(R"("finished_at":)") != std::string::npos);
    BOOST_TEST(json.find("job_cancelled") == std::string::npos);
}

// Type: unit (serializer shape, cancelled; partial result kept).
// Scenario: a cancelled record emits the fixed job_cancelled error and keeps
//   any partial result embedded as an object.
// Boundaries: cancelled carries error{job_cancelled} and may keep a partial.
// On failure first check: did the serializer stop emitting the fixed
//   job_cancelled error for a cancelled job?
BOOST_AUTO_TEST_CASE(serialize_cancelled_emits_job_cancelled_and_partial)
{
    JobRecord record = make_record(JobState::cancelled);
    record.finished_at = std::chrono::system_clock::time_point {} + 2s;
    record.result_json = R"({"partial":2})";
    const std::string json = serialize_job_json(record);

    BOOST_TEST(json.find(R"("state":"cancelled")") != std::string::npos);
    BOOST_TEST(json.find(
                   R"("error":{"code":"job_cancelled","message":"job cancelled"})")
               != std::string::npos);
    BOOST_TEST(json.find(R"("result":{"partial":2})") != std::string::npos);
    BOOST_TEST(json.find(R"("finished_at":)") != std::string::npos);
}

// Type: unit (job.get unknown id -> not_found).
// Scenario: job.get for an id the runner never issued -> not_found with the
//   fixed message, and the id is never echoed.
// Boundaries: unknown-id branch maps to not_found, not internal.
// On failure first check: did handle_job_get stop checking the optional before
//   serializing?
BOOST_AUTO_TEST_CASE(job_get_unknown_id_is_not_found)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});
    CommandRegistry registry;
    register_job_commands(registry, *runner);

    const CommandResponse resp = registry.execute(
        caller_with(ControlSet::User),
        request_for("job.get", R"({"id":"j-does-not-exist"})"));

    BOOST_TEST(resp.ok == false);
    BOOST_TEST((resp.error.code == CommandErrorCode::not_found));
    BOOST_TEST(resp.error.message == "job not found");
    BOOST_TEST(resp.error.message.find("j-does-not-exist")
               == std::string::npos);
}

// Type: unit (job.get live record wires the serializer).
// Scenario: job.get of a real running job returns a result whose JSON carries
//   the job id and state running.
// Boundaries: the handler serializes the live record via serialize_job_json.
// On failure first check: did handle_job_get stop calling serialize_job_json or
//   reading the live record?
BOOST_AUTO_TEST_CASE(job_get_running_job_returns_serialized_record)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});
    CommandRegistry registry;
    register_job_commands(registry, *runner);

    Gate gate;
    const std::string id = submit_gated(*runner, gate);
    BOOST_REQUIRE(wait_running(*runner, id));

    const CommandResponse resp = registry.execute(
        caller_with(ControlSet::User),
        request_for("job.get", R"({"id":")" + id + R"("})"));

    BOOST_TEST(resp.ok == true);
    BOOST_TEST(resp.result_json.find(R"("id":")" + id + R"(")")
               != std::string::npos);
    BOOST_TEST(resp.result_json.find(R"("state":"running")")
               != std::string::npos);

    gate.open->set_value();
    runner->wait_for_terminal(id, 10s);
}

// Type: unit (job.list default limit, filter, offset-beyond-total).
// Scenario: with three jobs, job.list defaults return all three with total 3; a
//   state filter narrows to matching jobs; an offset beyond total yields an
//   empty page with the correct total.
// Boundaries: default page, filter narrowing, offset >= total empty page.
// On failure first check: did handle_job_list stop forwarding the filter/offset
//   to runner.list, or stop emitting the page total?
BOOST_AUTO_TEST_CASE(job_list_default_filter_and_offset_beyond_total)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 8, 16});
    CommandRegistry registry;
    register_job_commands(registry, *runner);

    Gate g1;
    Gate g2;
    Gate g3;
    const std::string id1 = submit_gated(*runner, g1);
    const std::string id2 = submit_gated(*runner, g2);
    const std::string id3 = submit_gated(*runner, g3);
    (void)id1;
    (void)id2;
    (void)id3;

    const CallerContext caller = caller_with(ControlSet::User);

    // Default page: all three, total 3.
    const CommandResponse all = registry.execute(caller, request_for("job.list"));
    BOOST_TEST(all.ok == true);
    BOOST_TEST(all.result_json.find(R"("total":3)") != std::string::npos);

    // Filter by running: at least one running job, total matches the filter.
    BOOST_REQUIRE(wait_running(*runner, id1));
    const CommandResponse running = registry.execute(
        caller, request_for("job.list", R"({"state":"running"})"));
    BOOST_TEST(running.ok == true);
    BOOST_TEST(running.result_json.find(R"("state":"running")")
               != std::string::npos);

    // Offset beyond total: empty items, total preserved.
    const CommandResponse beyond = registry.execute(
        caller, request_for("job.list", R"({"offset":100})"));
    BOOST_TEST(beyond.ok == true);
    BOOST_TEST(beyond.result_json.find(R"("items":[])") != std::string::npos);
    BOOST_TEST(beyond.result_json.find(R"("total":3)") != std::string::npos);

    g1.open->set_value();
    g2.open->set_value();
    g3.open->set_value();
    runner->wait_for_terminal(id1, 10s);
    runner->wait_for_terminal(id2, 10s);
    runner->wait_for_terminal(id3, 10s);
}

// Type: unit (job.list invalid bounds and unknown state -> invalid_field).
// Scenario: limit 501, limit 0 and an unknown state token each fail at argument
//   validation with invalid_field before the runner is read.
// Boundaries: the registry ArgSpec caps reject out-of-range and bad enum.
// On failure first check: did make_job_list_args drop the limit bounds or the
//   state enum values?
BOOST_AUTO_TEST_CASE(job_list_bad_bounds_and_state_are_invalid_field)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});
    CommandRegistry registry;
    register_job_commands(registry, *runner);
    const CallerContext caller = caller_with(ControlSet::User);

    for (const char* args : {R"({"limit":501})", R"({"limit":0})",
                             R"({"state":"bogus"})"}) {
        const CommandResponse resp =
            registry.execute(caller, request_for("job.list", args));
        BOOST_TEST(resp.ok == false);
        BOOST_TEST((resp.error.code == CommandErrorCode::invalid_field));
    }
}

// Type: unit (job.cancel of a queued job).
// Scenario: with one worker busy on a running job, a second job stays queued;
//   cancelling it returns a cancelled record and its body never runs.
// Boundaries: queued -> cancelled immediately, no body execution.
// On failure first check: did runner.cancel stop finishing a queued job
//   cancelled, or did the handler stop serializing the returned record?
BOOST_AUTO_TEST_CASE(job_cancel_queued_job_ends_cancelled)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});
    CommandRegistry registry;
    register_job_commands(registry, *runner);

    Gate busy;
    Gate queued;
    const std::string busy_id = submit_gated(*runner, busy);
    BOOST_REQUIRE(wait_running(*runner, busy_id));
    const std::string queued_id = submit_gated(*runner, queued);

    const CommandResponse resp = registry.execute(
        caller_with(ControlSet::Admin),
        request_for("job.cancel", R"({"id":")" + queued_id + R"("})"));

    BOOST_TEST(resp.ok == true);
    BOOST_TEST(resp.result_json.find(R"("state":"cancelled")")
               != std::string::npos);
    BOOST_TEST(queued.saw_cancel->load() == false);  // body never ran

    busy.open->set_value();
    runner->wait_for_terminal(busy_id, 10s);
}

// Type: unit (job.cancel of a running job / T-90).
// Scenario: cancelling a running job returns it still running; the body then
//   observes the flag and the job settles cancelled.
// Boundaries: running -> flag set, record still running at return, later
//   cancelled.
// On failure first check: did runner.cancel stop setting the cooperative flag,
//   or did the handler misreport the transient state?
BOOST_AUTO_TEST_CASE(job_cancel_running_job_returns_running_then_cancels)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});
    CommandRegistry registry;
    register_job_commands(registry, *runner);

    Gate gate;
    const std::string id = submit_gated(*runner, gate);
    BOOST_REQUIRE(wait_running(*runner, id));

    const CommandResponse resp = registry.execute(
        caller_with(ControlSet::Admin),
        request_for("job.cancel", R"({"id":")" + id + R"("})"));
    BOOST_TEST(resp.ok == true);
    BOOST_TEST(resp.result_json.find(R"("state":"running")")
               != std::string::npos);

    const std::optional<JobRecord> final_record = runner->wait_for_terminal(id, 10s);
    BOOST_REQUIRE(final_record.has_value());
    BOOST_TEST((final_record->state == JobState::cancelled));
    BOOST_TEST(gate.saw_cancel->load() == true);
}

// Type: unit (job.cancel of a finished job is a no-op).
// Scenario: a job that already succeeded is cancelled; the call returns the
//   same final succeeded record and mutates nothing.
// Boundaries: terminal job -> no-op returning its final state.
// On failure first check: did runner.cancel start mutating a terminal record?
BOOST_AUTO_TEST_CASE(job_cancel_finished_job_is_noop)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});
    CommandRegistry registry;
    register_job_commands(registry, *runner);

    Gate gate;
    gate.open->set_value();  // let it finish immediately
    const std::string id = submit_gated(*runner, gate);
    const std::optional<JobRecord> done = runner->wait_for_terminal(id, 10s);
    BOOST_REQUIRE(done.has_value());
    BOOST_REQUIRE((done->state == JobState::succeeded));

    const CommandResponse resp = registry.execute(
        caller_with(ControlSet::Admin),
        request_for("job.cancel", R"({"id":")" + id + R"("})"));
    BOOST_TEST(resp.ok == true);
    BOOST_TEST(resp.result_json.find(R"("state":"succeeded")")
               != std::string::npos);
}

// Type: unit (job.cancel unknown id -> not_found).
// Scenario: cancelling an id the runner never issued -> not_found.
// Boundaries: unknown-id branch of cancel.
// On failure first check: did handle_job_cancel stop mapping a nullopt to
//   not_found?
BOOST_AUTO_TEST_CASE(job_cancel_unknown_id_is_not_found)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});
    CommandRegistry registry;
    register_job_commands(registry, *runner);

    const CommandResponse resp = registry.execute(
        caller_with(ControlSet::Admin),
        request_for("job.cancel", R"({"id":"j-nope"})"));
    BOOST_TEST(resp.ok == false);
    BOOST_TEST((resp.error.code == CommandErrorCode::not_found));
}

// Type: unit (authorization per descriptor / C7, T-92).
// Scenario: a ControlSet::User may job.get and job.list but is denied
//   not_authorized on job.cancel, which never reaches the runner.
// Boundaries: read vs write permission enforced by execute, not the handler.
// On failure first check: did make_job_cancel_descriptor lose its write
//   permission, or did read commands start requiring write?
BOOST_AUTO_TEST_CASE(user_may_read_but_not_cancel)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});
    CommandRegistry registry;
    register_job_commands(registry, *runner);
    const CallerContext user = caller_with(ControlSet::User);

    const CommandResponse list_resp =
        registry.execute(user, request_for("job.list"));
    BOOST_TEST(list_resp.ok == true);

    const CommandResponse get_resp = registry.execute(
        user, request_for("job.get", R"({"id":"j-1"})"));
    // A read is authorized; the id simply does not exist yet.
    BOOST_TEST((get_resp.error.code == CommandErrorCode::not_found));

    const CommandResponse cancel_resp = registry.execute(
        user, request_for("job.cancel", R"({"id":"j-1"})"));
    BOOST_TEST(cancel_resp.ok == false);
    BOOST_TEST((cancel_resp.error.code == CommandErrorCode::not_authorized));
}

// Type: unit (catalog registration / success criterion 2).
// Scenario: the three job commands register alongside command.list with perms
//   read, read, write and appear in command.list output.
// Boundaries: the registry exposes job.get/list/cancel with the catalog perms.
// On failure first check: did register_job_commands drop a descriptor or change
//   a permission?
BOOST_AUTO_TEST_CASE(job_commands_register_with_catalog_permissions)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});
    CommandRegistry registry;
    register_builtin_commands(registry);
    register_job_commands(registry, *runner);

    const CommandDescriptor* get = registry.find("job.get");
    const CommandDescriptor* list = registry.find("job.list");
    const CommandDescriptor* cancel = registry.find("job.cancel");
    BOOST_REQUIRE(get != nullptr);
    BOOST_REQUIRE(list != nullptr);
    BOOST_REQUIRE(cancel != nullptr);
    BOOST_TEST((get->permission == Permission::read));
    BOOST_TEST((list->permission == Permission::read));
    BOOST_TEST((cancel->permission == Permission::write));
    BOOST_TEST(cancel->mutating == true);

    const CommandResponse resp =
        registry.execute(caller_with(ControlSet::User),
                         request_for("command.list"));
    BOOST_TEST(resp.ok == true);
    BOOST_TEST(resp.result_json.find(R"("name":"job.get")")
               != std::string::npos);
    BOOST_TEST(resp.result_json.find(R"("name":"job.list")")
               != std::string::npos);
    BOOST_TEST(resp.result_json.find(R"("name":"job.cancel")")
               != std::string::npos);
}
