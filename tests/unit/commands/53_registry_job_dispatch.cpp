#define BOOST_TEST_MODULE CommandsRegistryJobDispatchTest
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/arg_spec.hpp"
#include "url_shortener/commands/audit.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/job_runner.hpp"
#include "url_shortener/commands/job_submitter.hpp"
#include "url_shortener/commands/job_types.hpp"
#include "url_shortener/core/clock.hpp"

using url_shortener::commands::ArgSpec;
using url_shortener::commands::ArgsView;
using url_shortener::commands::ArgType;
using url_shortener::commands::AuditRecord;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandContext;
using url_shortener::commands::CommandDescriptor;
using url_shortener::commands::CommandError;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::ExecutionKind;
using url_shortener::commands::IAuditSink;
using url_shortener::commands::IJobContext;
using url_shortener::commands::IJobSubmitter;
using url_shortener::commands::is_terminal;
using url_shortener::commands::JobBody;
using url_shortener::commands::JobOutcome;
using url_shortener::commands::JobRecord;
using url_shortener::commands::JobRunner;
using url_shortener::commands::JobRunnerOptions;
using url_shortener::commands::JobState;
using url_shortener::commands::make_error;
using url_shortener::commands::Permission;
using url_shortener::commands::serialize_response;

namespace
{

// A clock frozen at the epoch; the runner reads it only for record timestamps
// and never mutates it, so concurrent reads from workers are race-free.
ManualClock make_clock()
{
    return ManualClock(std::chrono::system_clock::time_point {});
}

// Shared observation state for a gated job body. Every member is a
// `std::shared_ptr` so the body captures only shared ownership and plain values
// -- never a reference to the registry, runtime or config (threat model T-87).
// The test and the body therefore observe the same cells across threads.
struct GatedProbe
{
    // One-shot gate: a running body blocks until the test fulfils this promise.
    std::shared_ptr<std::promise<void>> gate =
        std::make_shared<std::promise<void>>();

    // Times the `job_factory` was entered (0 proves it was never reached).
    std::shared_ptr<std::atomic<int>> factory_calls =
        std::make_shared<std::atomic<int>>(0);

    // The value `IJobContext::cancel_requested()` returned to the body.
    std::shared_ptr<std::atomic<bool>> saw_cancel =
        std::make_shared<std::atomic<bool>>(false);

    // The thread the body ran on (the request thread for a dry_run, a worker
    // thread for a real job); written once by the body before it finishes.
    std::shared_ptr<std::thread::id> ran_on =
        std::make_shared<std::thread::id>();
};

// Builds a `kind == job` descriptor whose factory copies the `path` arg into a
// self-contained body (threat model T-87/T-12): the body waits on the probe's
// gate while cooperatively polling `cancel_requested()`, reports progress and
// returns the copied value in its `result_json`. The shared_future is derived
// once here (never per factory call) so a descriptor may be executed more than
// once without re-calling `std::promise::get_future`.
//
// @param name Dotted command name for the descriptor.
// @param permission Base permission the command requires.
// @param probe Shared observation cells, captured by value into the body.
// @param supports_dry_run When true the descriptor is mutating and honours
// `options.dry_run` (so the registry runs the body synchronously with a no-op
// context, never the submitter).
// @return The configured descriptor, with a single optional `path` string arg.
CommandDescriptor make_gated_job_descriptor(std::string name,
                                            Permission permission,
                                            GatedProbe probe,
                                            bool supports_dry_run = false)
{
    const std::shared_future<void> gate_future =
        probe.gate->get_future().share();

    CommandDescriptor d;
    d.name = std::move(name);
    d.summary = "a gated job";
    d.permission = permission;
    d.kind = ExecutionKind::job;
    d.mutating = permission != Permission::read;
    d.supports_dry_run = supports_dry_run;

    ArgSpec path;
    path.name = "path";
    path.type = ArgType::string;
    d.args = {path};

    d.job_factory =
        [probe, gate_future](const CommandContext&,
                             const ArgsView& view)
        -> std::variant<JobBody, CommandError>
    {
        probe.factory_calls->fetch_add(1);
        // Deep-copy the arg into the body so it holds no request-owned memory.
        const std::string copied = view.get_string("path").value_or("none");
        return JobBody {
            [probe, gate_future, copied](IJobContext& ctx) -> JobOutcome
            {
                *probe.ran_on = std::this_thread::get_id();
                ctx.set_total(10);
                ctx.advance(5);  // partial progress before the gate
                for (;;) {
                    if (ctx.cancel_requested()) {
                        probe.saw_cancel->store(true);
                        JobOutcome out;
                        out.ok = false;
                        out.cancelled = true;
                        out.result_json =
                            std::string(R"({"partial":")") + copied + "\"}";
                        return out;
                    }
                    if (gate_future.wait_for(std::chrono::milliseconds(0))
                        == std::future_status::ready)
                    {
                        break;
                    }
                    std::this_thread::yield();
                }
                probe.saw_cancel->store(ctx.cancel_requested());
                ctx.advance(5);
                JobOutcome out;
                out.ok = true;
                out.result_json =
                    std::string(R"({"copied":")") + copied + "\"}";
                return out;
            }};
    };
    return d;
}

// Captures every AuditRecord the registry emits, in emission order. Lives on
// the test stack and is installed via a non-owning aliasing shared_ptr.
struct CapturingAuditSink : public IAuditSink
{
    std::vector<AuditRecord> records;

    void record(const AuditRecord& record) noexcept override
    {
        records.push_back(record);
    }
};

// Shares a stack-owned sink with a registry without transferring ownership.
std::shared_ptr<IAuditSink> alias(CapturingAuditSink& sink)
{
    return std::shared_ptr<IAuditSink>(std::shared_ptr<void>(), &sink);
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

// Spins (never sleeps) until the job is observed `running`, then returns true;
// bounded by a generous ceiling so a regression fails rather than hangs.
bool wait_running(const JobRunner& runner, const std::string& id)
{
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        const std::optional<JobRecord> record = runner.get(id);
        if (record.has_value() && record->state == JobState::running) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

}  // namespace

// Type: unit (registry + real JobRunner, happy path / C2).
// Scenario: a kind==job command with the real runner injected returns ok:true
//   with job.id and state "queued", no result, and exactly one of
//   result|job|error in the serialized envelope.
// Boundaries: accepted-job response shape.
// On failure first check: did resolve_dispatch stop mapping kind==job to the
//   job path, or did make_job_accepted stop clearing result_json?
BOOST_AUTO_TEST_CASE(job_command_returns_queued_job_and_no_result)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 1, 16});

    CommandRegistry registry;
    registry.set_job_submitter(runner);
    GatedProbe probe;
    registry.add(make_gated_job_descriptor("link.export", Permission::read,
                                           probe));

    const CommandResponse resp = registry.execute(
        caller_with(ControlSet::User),
        request_for("link.export", R"({"path":"marker-1"})"));

    BOOST_TEST(resp.ok == true);
    BOOST_REQUIRE(resp.job.has_value());
    BOOST_TEST(!resp.job->id.empty());
    BOOST_TEST((resp.job->state == JobState::queued));
    BOOST_TEST(resp.result_json.empty() == true);
    BOOST_TEST((resp.error.code == CommandErrorCode::none));

    // Exactly one payload in the wire envelope: job, never result or error.
    const std::string wire = serialize_response(resp);
    BOOST_TEST(wire.find(R"("job":)") != std::string::npos);
    BOOST_TEST(wire.find(R"("result":)") == std::string::npos);
    BOOST_TEST(wire.find(R"("error":)") == std::string::npos);

    probe.gate->set_value();  // let the body finish so shutdown is prompt
    runner->wait_for_terminal(resp.job->id, std::chrono::seconds(10));
}

// Type: unit (end-to-end job completion on a worker).
// Scenario: the accepted job reaches succeeded in runner->get(id) with the
//   body's result_json, and it ran on a worker thread (not the request thread).
// Boundaries: queued -> running -> succeeded through the real pool.
// On failure first check: did the factory stop copying the arg into the body,
//   or did run_job stop forwarding result_json to the store?
BOOST_AUTO_TEST_CASE(accepted_job_reaches_succeeded_with_result)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});

    CommandRegistry registry;
    registry.set_job_submitter(runner);
    GatedProbe probe;
    registry.add(make_gated_job_descriptor("link.export", Permission::read,
                                           probe));

    const CommandResponse resp = registry.execute(
        caller_with(ControlSet::User),
        request_for("link.export", R"({"path":"payload-A"})"));
    BOOST_REQUIRE(resp.job.has_value());

    probe.gate->set_value();
    const std::optional<JobRecord> final_record =
        runner->wait_for_terminal(resp.job->id, std::chrono::seconds(10));
    BOOST_REQUIRE(final_record.has_value());
    BOOST_TEST(is_terminal(final_record->state));
    BOOST_TEST((final_record->state == JobState::succeeded));
    BOOST_REQUIRE(final_record->result_json.has_value());
    BOOST_TEST(final_record->result_json->find("payload-A")
               != std::string::npos);
    // The body ran on a worker, not the caller thread.
    BOOST_TEST((*probe.ran_on != std::this_thread::get_id()));
    BOOST_TEST(probe.saw_cancel->load() == false);
}

// Type: unit (authorization precedes the factory / C7, T-12).
// Scenario: a ControlSet::User calling a write job -> not_authorized and the
//   job_factory is never invoked; nothing is queued.
// Boundaries: permission check at step 3, before the job path.
// On failure first check: did execute start reaching dispatch before the
//   permission check, or did the write descriptor lose its write permission?
BOOST_AUTO_TEST_CASE(user_write_job_is_not_authorized_and_factory_not_invoked)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});

    CommandRegistry registry;
    registry.set_job_submitter(runner);
    GatedProbe probe;
    registry.add(make_gated_job_descriptor("link.import", Permission::write,
                                           probe));

    const CommandResponse resp = registry.execute(
        caller_with(ControlSet::User),
        request_for("link.import", R"({"path":"x"})"));

    BOOST_TEST(resp.ok == false);
    BOOST_TEST((resp.error.code == CommandErrorCode::not_authorized));
    BOOST_TEST(probe.factory_calls->load() == 0);
    BOOST_TEST(runner->list(std::nullopt, 0, 100).total == 0u);
}

// Type: unit (validation precedes the factory / C7, T-12).
// Scenario: an undeclared argument -> invalid_field and the factory is never
//   invoked; nothing is queued.
// Boundaries: arg validation at step 4, before the job path.
// On failure first check: did dispatch move ahead of validate_args?
BOOST_AUTO_TEST_CASE(invalid_args_is_invalid_field_and_factory_not_invoked)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});

    CommandRegistry registry;
    registry.set_job_submitter(runner);
    GatedProbe probe;
    registry.add(make_gated_job_descriptor("link.export", Permission::read,
                                           probe));

    const CommandResponse resp = registry.execute(
        caller_with(ControlSet::User),
        request_for("link.export", R"({"bogus":"x"})"));

    BOOST_TEST(resp.ok == false);
    BOOST_TEST((resp.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(probe.factory_calls->load() == 0);
    BOOST_TEST(runner->list(std::nullopt, 0, 100).total == 0u);
}

// Type: unit (factory failure / T-12).
// Scenario: a factory that returns a CommandError surfaces that failure, queues
//   nothing and leaves no job record in the real runner.
// Boundaries: factory error vs submit; the "either a job or an error" contract.
// On failure first check: did submit_job start calling the submitter after a
//   factory error, or create a record speculatively?
BOOST_AUTO_TEST_CASE(factory_error_propagates_and_nothing_is_queued)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});

    CommandRegistry registry;
    registry.set_job_submitter(runner);

    // A job descriptor whose factory always returns an error before a body.
    CommandDescriptor d;
    d.name = "link.export";
    d.summary = "a failing factory";
    d.kind = ExecutionKind::job;
    std::atomic<int> factory_calls {0};
    d.job_factory =
        [&factory_calls](const CommandContext&, const ArgsView&)
        -> std::variant<JobBody, CommandError>
    {
        factory_calls.fetch_add(1);
        return make_error(CommandErrorCode::invalid_url, "factory failed");
    };
    registry.add(std::move(d));

    const CommandResponse resp = registry.execute(
        caller_with(ControlSet::User), request_for("link.export"));

    BOOST_TEST(resp.ok == false);
    BOOST_TEST((resp.error.code == CommandErrorCode::invalid_url));
    BOOST_TEST(factory_calls.load() == 1);
    BOOST_TEST(runner->list(std::nullopt, 0, 100).total == 0u);
}

// Type: unit (fail-closed with no submitter / T-12).
// Scenario: a registry with no submitter installed yields unsupported for a job
//   command and never reaches the factory.
// Boundaries: null submitter at the job path.
// On failure first check: did submit_job stop fail-closing before touching the
//   factory when m_job_submitter is null?
BOOST_AUTO_TEST_CASE(no_submitter_yields_unsupported_and_factory_not_invoked)
{
    CommandRegistry registry;  // no set_job_submitter
    GatedProbe probe;
    registry.add(make_gated_job_descriptor("link.export", Permission::read,
                                           probe));

    const CommandResponse resp = registry.execute(
        caller_with(ControlSet::User),
        request_for("link.export", R"({"path":"x"})"));

    BOOST_TEST(resp.ok == false);
    BOOST_TEST((resp.error.code == CommandErrorCode::unsupported));
    BOOST_TEST(probe.factory_calls->load() == 0);
}

// Type: unit (bounded queue / T-86).
// Scenario: with one worker and a one-deep queue, a running job plus one queued
//   job fill capacity; a third dispatch returns the busy envelope and leaves
//   runner->list unchanged (no record created).
// Boundaries: queue.size() == queue_capacity is the reject edge, seen through
//   execute rather than submit directly.
// On failure first check: did submit start accepting past capacity, or create a
//   record before the capacity check?
BOOST_AUTO_TEST_CASE(full_queue_yields_busy_and_list_is_unchanged)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 1, 16});

    CommandRegistry registry;
    registry.set_job_submitter(runner);
    GatedProbe probe;
    registry.add(make_gated_job_descriptor("link.export", Permission::read,
                                           probe));

    CallerContext caller = caller_with(ControlSet::User);
    const CommandResponse a = registry.execute(
        caller, request_for("link.export", R"({"path":"a"})"));
    BOOST_REQUIRE(a.job.has_value());
    BOOST_REQUIRE(wait_running(*runner, a.job->id));  // worker now busy

    const CommandResponse b = registry.execute(
        caller, request_for("link.export", R"({"path":"b"})"));
    BOOST_REQUIRE(b.job.has_value());  // fills the one queue slot
    BOOST_TEST(runner->list(std::nullopt, 0, 100).total == 2u);

    const CommandResponse c = registry.execute(
        caller, request_for("link.export", R"({"path":"c"})"));
    BOOST_TEST(c.ok == false);
    BOOST_TEST((c.error.code == CommandErrorCode::busy));
    BOOST_TEST(c.job.has_value() == false);
    // The rejected dispatch created no record.
    BOOST_TEST(runner->list(std::nullopt, 0, 100).total == 2u);

    probe.gate->set_value();
    runner->wait_for_terminal(a.job->id, std::chrono::seconds(10));
    runner->wait_for_terminal(b.job->id, std::chrono::seconds(10));
}

// Type: unit (async on a non-capable sync command).
// Scenario: options.async on a plain sync command (not async_capable) ->
//   unsupported; the submitter is never touched.
// Boundaries: async flag on a sync, non-async-capable descriptor.
// On failure first check: did resolve_dispatch start treating async as capable
//   by default?
BOOST_AUTO_TEST_CASE(async_on_non_capable_sync_command_is_unsupported)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});

    CommandRegistry registry;
    registry.set_job_submitter(runner);

    CommandDescriptor d;
    d.name = "link.get";
    d.summary = "a plain read";
    d.permission = Permission::read;
    d.handler = [](CommandContext&, const ArgsView&)
    {
        url_shortener::commands::CommandResult r;
        r.result_json = R"({"ok":true})";
        return r;
    };
    registry.add(std::move(d));

    CommandRequest req = request_for("link.get");
    req.options.async = true;
    const CommandResponse resp =
        registry.execute(caller_with(ControlSet::User), req);

    BOOST_TEST(resp.ok == false);
    BOOST_TEST((resp.error.code == CommandErrorCode::unsupported));
    BOOST_TEST(resp.error.message == "async not supported");
    BOOST_TEST(runner->list(std::nullopt, 0, 100).total == 0u);
}

// Type: unit (async on an async_capable sync command runs via the runner).
// Scenario: options.async on an async_capable command builds the body through
//   the job_factory and runs it on the real runner to completion.
// Boundaries: async flag routes a sync command through the job path.
// On failure first check: did resolve_dispatch stop routing async_capable to
//   the submitter, or did the sync handler run instead of the factory body?
BOOST_AUTO_TEST_CASE(async_on_async_capable_command_runs_via_runner)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});

    CommandRegistry registry;
    registry.set_job_submitter(runner);

    // Start from a job descriptor, then flip it to a sync command that is
    // async_capable (still needs a job_factory, which the helper provided).
    GatedProbe probe;
    CommandDescriptor d =
        make_gated_job_descriptor("link.get", Permission::read, probe);
    d.kind = ExecutionKind::sync;
    d.async_capable = true;
    d.handler = [](CommandContext&, const ArgsView&)
    {
        url_shortener::commands::CommandResult r;
        r.result_json = R"({"sync":true})";
        return r;
    };
    registry.add(std::move(d));

    CommandRequest req = request_for("link.get", R"({"path":"async-payload"})");
    req.options.async = true;
    const CommandResponse resp =
        registry.execute(caller_with(ControlSet::User), req);

    BOOST_TEST(resp.ok == true);
    BOOST_REQUIRE(resp.job.has_value());
    BOOST_TEST(probe.factory_calls->load() == 1);

    probe.gate->set_value();
    const std::optional<JobRecord> final_record =
        runner->wait_for_terminal(resp.job->id, std::chrono::seconds(10));
    BOOST_REQUIRE(final_record.has_value());
    BOOST_TEST((final_record->state == JobState::succeeded));
    BOOST_REQUIRE(final_record->result_json.has_value());
    // The async path used the factory body, not the sync handler.
    BOOST_TEST(final_record->result_json->find("async-payload")
               != std::string::npos);
}

// Type: unit (self-contained body / T-12, T-87).
// Scenario: the request buffer is destroyed immediately after execute, then the
//   gated body is released; it still reports the original argument value, so the
//   factory deep-copied every value into the body.
// Boundaries: body outlives the request-owned args buffer.
// On failure first check: did the factory start capturing the ArgsView or the
//   request by reference instead of copying the decoded value?
BOOST_AUTO_TEST_CASE(body_is_self_contained_after_request_destroyed)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});

    CommandRegistry registry;
    registry.set_job_submitter(runner);
    GatedProbe probe;
    registry.add(make_gated_job_descriptor("link.export", Permission::read,
                                           probe));

    std::string job_id;
    {
        // The request and its args buffer live only in this scope.
        auto request = std::make_unique<CommandRequest>(
            request_for("link.export", R"({"path":"/tmp/secret-path"})"));
        const CommandResponse resp =
            registry.execute(caller_with(ControlSet::User), *request);
        BOOST_REQUIRE(resp.job.has_value());
        job_id = resp.job->id;
        request.reset();  // destroy the args source before the body runs
    }

    probe.gate->set_value();
    const std::optional<JobRecord> final_record =
        runner->wait_for_terminal(job_id, std::chrono::seconds(10));
    BOOST_REQUIRE(final_record.has_value());
    BOOST_TEST((final_record->state == JobState::succeeded));
    BOOST_REQUIRE(final_record->result_json.has_value());
    BOOST_TEST(final_record->result_json->find("/tmp/secret-path")
               != std::string::npos);
}

// Type: unit (cooperative cancel through the runner / T-90).
// Scenario: a running job is cancelled via runner->cancel while gated; it ends
//   cancelled and keeps the partial result_json the body produced.
// Boundaries: running -> cancel flag -> body-driven cancelled; partial kept.
// On failure first check: did cancel stop setting the flag, or did the body
//   stop polling cancel_requested()?
BOOST_AUTO_TEST_CASE(cancel_running_job_ends_cancelled_with_partial_result)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});

    CommandRegistry registry;
    registry.set_job_submitter(runner);
    GatedProbe probe;
    registry.add(make_gated_job_descriptor("link.export", Permission::read,
                                           probe));

    const CommandResponse resp = registry.execute(
        caller_with(ControlSet::User),
        request_for("link.export", R"({"path":"half"})"));
    BOOST_REQUIRE(resp.job.has_value());
    BOOST_REQUIRE(wait_running(*runner, resp.job->id));

    const std::optional<JobRecord> during = runner->cancel(resp.job->id);
    BOOST_REQUIRE(during.has_value());
    BOOST_TEST((during->state == JobState::running));

    const std::optional<JobRecord> final_record =
        runner->wait_for_terminal(resp.job->id, std::chrono::seconds(10));
    BOOST_REQUIRE(final_record.has_value());
    BOOST_TEST((final_record->state == JobState::cancelled));
    BOOST_TEST(probe.saw_cancel->load() == true);
    BOOST_REQUIRE(final_record->result_json.has_value());
    BOOST_TEST(final_record->result_json->find("half") != std::string::npos);
}

// Type: unit (one shared runner across registries / REST per-request case).
// Scenario: two registries sharing the same shared_ptr<JobRunner> submit into
//   the one runtime; both jobs are visible in runner->list and by id, proving a
//   job outlives the per-request registry that submitted it.
// Boundaries: two CommandRegistry instances, one JobRunner.
// On failure first check: did set_job_submitter start copying rather than
//   sharing the runner, or does the runner hold per-registry state?
BOOST_AUTO_TEST_CASE(two_registries_share_one_runner)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});

    CommandRegistry registry_a;
    CommandRegistry registry_b;
    registry_a.set_job_submitter(runner);
    registry_b.set_job_submitter(runner);
    GatedProbe probe_a;
    GatedProbe probe_b;
    registry_a.add(make_gated_job_descriptor("link.export", Permission::read,
                                             probe_a));
    registry_b.add(make_gated_job_descriptor("link.export", Permission::read,
                                             probe_b));

    const CommandResponse ra = registry_a.execute(
        caller_with(ControlSet::User),
        request_for("link.export", R"({"path":"from-a"})"));
    const CommandResponse rb = registry_b.execute(
        caller_with(ControlSet::User),
        request_for("link.export", R"({"path":"from-b"})"));
    BOOST_REQUIRE(ra.job.has_value());
    BOOST_REQUIRE(rb.job.has_value());
    BOOST_TEST(ra.job->id != rb.job->id);

    // Both jobs live in the single shared runner, each resolvable by id.
    BOOST_TEST(runner->list(std::nullopt, 0, 100).total == 2u);
    BOOST_TEST(runner->get(ra.job->id).has_value());
    BOOST_TEST(runner->get(rb.job->id).has_value());

    probe_a.gate->set_value();
    probe_b.gate->set_value();
    runner->wait_for_terminal(ra.job->id, std::chrono::seconds(10));
    runner->wait_for_terminal(rb.job->id, std::chrono::seconds(10));
}

// Type: unit (audit records acceptance only / C6, T-108).
// Scenario: a mutating job accepted through the runner emits exactly one audit
//   record with outcome "job_accepted" and target == the job id; no argument
//   value appears in any audit field.
// Boundaries: one record per attempt; job id is the target.
// On failure first check: did derive_audit_outcome stop returning job_accepted,
//   or did the registry start placing arg values in the audit target?
BOOST_AUTO_TEST_CASE(audit_records_job_accepted_with_id_and_no_arg_values)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});

    CommandRegistry registry;
    registry.set_job_submitter(runner);
    CapturingAuditSink sink;
    registry.set_audit_sink(alias(sink));
    GatedProbe probe;
    // A mutating (write) job so the audit line is emitted.
    registry.add(make_gated_job_descriptor("link.import", Permission::write,
                                           probe));

    const CommandResponse resp = registry.execute(
        caller_with(ControlSet::Admin),
        request_for("link.import", R"({"path":"secret-marker"})"));
    BOOST_REQUIRE(resp.job.has_value());

    BOOST_REQUIRE(sink.records.size() == 1u);
    const AuditRecord& record = sink.records.front();
    BOOST_TEST(record.command == "link.import");
    BOOST_TEST(record.outcome == "job_accepted");
    BOOST_TEST(record.target == resp.job->id);
    // The argument value never reaches any audit field.
    BOOST_TEST(record.target.find("secret-marker") == std::string::npos);
    BOOST_TEST(record.principal.find("secret-marker") == std::string::npos);

    probe.gate->set_value();
    runner->wait_for_terminal(resp.job->id, std::chrono::seconds(10));
}

// Type: unit (dry_run is synchronous, never a job / T-21 carry-forward).
// Scenario: a supports_dry_run job command run with options.dry_run executes
//   its body synchronously on the request thread with a no-op context
//   (cancel_requested() == false), returns a result (not a job), submits
//   nothing to the runner and creates no job record.
// Boundaries: dry_run routes a job command to the synchronous NoopJobContext
//   path, not the submitter.
// On failure first check: did resolve_dispatch route dry_run on a kind==job
//   command to dispatch::job instead of dispatch::dry_run_job?
BOOST_AUTO_TEST_CASE(dry_run_job_runs_synchronously_with_noop_context_no_job)
{
    ManualClock clock = make_clock();
    auto runner = std::make_shared<JobRunner>(clock, JobRunnerOptions {1, 4, 16});

    CommandRegistry registry;
    registry.set_job_submitter(runner);
    GatedProbe probe;
    // supports_dry_run requires a mutating command; use write.
    registry.add(make_gated_job_descriptor("link.import", Permission::write,
                                           probe, /*supports_dry_run=*/true));

    // Pre-open the gate so the synchronous body completes without blocking the
    // request thread (a dry_run never cancels, so it cannot break out early).
    probe.gate->set_value();

    CommandRequest req =
        request_for("link.import", R"({"path":"dry-marker"})");
    req.options.dry_run = true;
    const CommandResponse resp =
        registry.execute(caller_with(ControlSet::Admin), req);

    BOOST_TEST(resp.ok == true);
    BOOST_TEST(resp.job.has_value() == false);  // a result, never a job
    BOOST_TEST(resp.result_json.find("dry-marker") != std::string::npos);
    BOOST_TEST(probe.factory_calls->load() == 1);
    // No job was submitted and no record created (T-21: no async path).
    BOOST_TEST(runner->list(std::nullopt, 0, 100).total == 0u);
    // The body saw a non-cancellable, no-op context on the request thread.
    BOOST_TEST(probe.saw_cancel->load() == false);
    BOOST_TEST((*probe.ran_on == std::this_thread::get_id()));
}
