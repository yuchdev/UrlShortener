#define BOOST_TEST_MODULE CommandsCommandRuntimeJobsTest
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/app/app_error.hpp"
#include "url_shortener/app/link_backend.hpp"
#include "url_shortener/app/link_command_service.hpp"
#include "url_shortener/app/link_lister.hpp"
#include "url_shortener/commands/audit.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/job_runner.hpp"
#include "url_shortener/commands/job_submitter.hpp"
#include "url_shortener/commands/job_types.hpp"
#include "url_shortener/composition/command_runtime.hpp"
#include "url_shortener/composition/job_runtime.hpp"
#include "url_shortener/composition/link_backend_bootstrap.hpp"
#include "url_shortener/core/config.hpp"

using url_shortener::app::ILinkLister;
using url_shortener::app::LinkListPage;
using url_shortener::app::LinkListQuery;
using url_shortener::commands::AuditRecord;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::IAuditSink;
using url_shortener::commands::IJobContext;
using url_shortener::commands::JobBody;
using url_shortener::commands::JobOutcome;
using url_shortener::commands::JobRecord;
using url_shortener::commands::JobRunner;
using url_shortener::commands::JobState;
using url_shortener::commands::JobSubmitResult;
using url_shortener::composition::build_command_runtime;
using url_shortener::composition::CommandRuntime;
using url_shortener::composition::CommandRuntimeOptions;
using url_shortener::composition::configure_process_job_runtime;
using url_shortener::composition::JobRuntimeSettings;
using url_shortener::composition::PairFileOptions;
using url_shortener::composition::process_job_runner;
using url_shortener::composition::reset_process_job_runtime_for_tests;
using url_shortener::composition::shutdown_process_job_runtime;

namespace app = url_shortener::app;

namespace
{

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

CallerContext caller(ControlSet role, const char* transport = "test")
{
    CallerContext c;
    c.principal = "test-principal";
    c.transport = transport;
    c.control_set = role;
    return c;
}

CommandRequest request(std::string command, std::string args_json)
{
    CommandRequest req;
    req.id = "client-1";
    req.command = std::move(command);
    req.args_json = std::move(args_json);
    return req;
}

// Records every audit line the registry emits, for the acceptance assertions.
class RecordingAuditSink : public IAuditSink
{
public:
    std::vector<AuditRecord> records;
    void record(const AuditRecord& record) noexcept override
    {
        records.push_back(record);
    }
};

// Creates a link through the runtime as Admin so a later link.export has rows;
// `slug` is expected to be unique across the executable's process-global store.
void seed_link(const CommandRuntime& runtime, const std::string& slug)
{
    const CommandResponse created = runtime.registry->execute(
        caller(ControlSet::Admin),
        request("link.create",
                R"({"target_url":"https://example.com/)" + slug
                    + R"(","slug":")" + slug + R"("})"));
    BOOST_REQUIRE(created.ok);
}

// Blocks a worker until `gate` flips, polling briefly so the test stays
// responsive; used only off the main thread.
void wait_for_gate(const std::shared_ptr<std::atomic<bool>>& gate)
{
    while (!gate->load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

}  // namespace

// Type: unit (one process-wide runner shared by every runtime; jobs cross it).
// Scenario: two runtimes built back to back report the same job_runner pointer;
//   a link.export submitted through runtime A is readable with job.get through
//   runtime B (the process-wide store, milestone C11).
// Boundaries: both runtimes wire process_job_runner(); the job lives in the
//   shared store, not in either runtime.
// On failure first check: did build_command_runtime stop sharing the runner, or
//   did the jobs store become per-runtime?
BOOST_AUTO_TEST_CASE(process_wide_runner_shared_and_job_visible_across_runtimes)
{
    reset_process_job_runtime_for_tests();
    url_shortener::app::LinkBackend::reset();
    ServerConfig server;
    url_shortener::app::LinkBackendHandle backend;

    const CommandRuntime runtime_a = build_command_runtime(server, backend);
    const CommandRuntime runtime_b = build_command_runtime(server, backend);

    BOOST_REQUIRE(runtime_a.job_runner != nullptr);
    BOOST_TEST((runtime_a.job_runner == runtime_b.job_runner));

    seed_link(runtime_a, "jobs64-across");

    const CommandResponse accepted = runtime_a.registry->execute(
        caller(ControlSet::Admin), request("link.export", R"({"format":"csv"})"));
    BOOST_REQUIRE(accepted.job.has_value());
    const std::string job_id = accepted.job->id;

    const std::optional<JobRecord> terminal =
        runtime_a.job_runner->wait_for_terminal(job_id,
                                                std::chrono::seconds(20));
    BOOST_REQUIRE(terminal.has_value());
    BOOST_TEST((terminal->state == JobState::succeeded));

    // The job is visible through runtime B's registry (same process-wide store).
    const CommandResponse got = runtime_b.registry->execute(
        caller(ControlSet::Admin),
        request("job.get", R"({"id":")" + job_id + R"("})"));
    BOOST_REQUIRE(got.ok);
    BOOST_TEST(contains(got.result_json, job_id));
    BOOST_TEST(contains(got.result_json, R"("state":"succeeded")"));

    reset_process_job_runtime_for_tests();
}

// Type: unit (T-87: a job outlives the runtime that submitted it; no dangling).
// Scenario: a gated job captures the runtime's shared lister + service (exactly
//   the ownership link.export's page source holds), the runtime is destroyed,
//   the gate opens, and the job still lists the seeded links and finishes
//   succeeded with the expected row count. Run under ASan where available.
// Boundaries: the lister holds the LinkCommandService by a raw reference, so the
//   body must keep the service shared-alive or it dereferences freed memory.
// On failure first check: did the export page-source capture drop the service
//   shared_ptr (list_page_from), re-introducing the use-after-free?
BOOST_AUTO_TEST_CASE(job_outlives_runtime_without_dangling)
{
    reset_process_job_runtime_for_tests();
    url_shortener::app::LinkBackend::reset();
    ServerConfig server;
    url_shortener::app::LinkBackendHandle backend;

    // Keep the runner alive independently of the runtime (as the process does).
    std::shared_ptr<JobRunner> runner = process_job_runner();

    // Seed three uniquely-named links through a runtime we then destroy.
    std::shared_ptr<ILinkLister> lister;
    std::shared_ptr<app::LinkCommandService> service;
    {
        const CommandRuntime runtime = build_command_runtime(server, backend);
        BOOST_REQUIRE(runtime.job_runner == runner);
        seed_link(runtime, "jobs64-life-a");
        seed_link(runtime, "jobs64-life-b");
        seed_link(runtime, "jobs64-life-c");
        // Capture the same shared ownership the export page source captures.
        lister = runtime.link_lister;
        service = runtime.link_service;
    }  // runtime (and its registry) destroyed here; runner/lister/service live.

    auto gate = std::make_shared<std::atomic<bool>>(false);
    JobBody body = [gate, lister, service](IJobContext&) -> JobOutcome
    {
        wait_for_gate(gate);
        // Use the lister AFTER the runtime is gone: it projects views through
        // the service it holds by raw reference, which `service` keeps alive.
        LinkListQuery query;
        query.q = "jobs64-life";  // matches only the three seeded above
        query.limit = 500;
        const app::Result<LinkListPage> page = lister->list(query);
        JobOutcome outcome;
        outcome.ok = page.ok();
        if (page.ok() && page.value.has_value()) {
            outcome.result_json =
                R"({"rows":)" + std::to_string(page.value->total) + "}";
        }
        return outcome;
    };

    const JobSubmitResult submitted =
        runner->submit("link.export", "digest", std::move(body));
    BOOST_REQUIRE(submitted.job.has_value());
    const std::string job_id = submitted.job->id;

    gate->store(true);  // open the gate now that the runtime is destroyed

    const std::optional<JobRecord> terminal =
        runner->wait_for_terminal(job_id, std::chrono::seconds(20));
    BOOST_REQUIRE(terminal.has_value());
    BOOST_TEST((terminal->state == JobState::succeeded));
    BOOST_REQUIRE(terminal->result_json.has_value());
    BOOST_TEST(contains(*terminal->result_json, R"("rows":3)"));

    runner.reset();
    reset_process_job_runtime_for_tests();
}

// Type: unit (the runtime registers the full job/pair command surface + table).
// Scenario: command.list contains the nine link commands, link.list, db.tables,
//   db.rows, system.info, command.list, job.get/list/cancel and
//   link.import/export; db.tables lists links, click_events and jobs.
// Boundaries: the composition root wires jobs into the one registry and the one
//   table catalog (C11).
// On failure first check: did a registration drop out of build_command_runtime,
//   or did the jobs table stop joining the catalog?
BOOST_AUTO_TEST_CASE(runtime_registers_job_commands_and_jobs_table)
{
    reset_process_job_runtime_for_tests();
    url_shortener::app::LinkBackend::reset();
    ServerConfig server;
    url_shortener::app::LinkBackendHandle backend;
    const CommandRuntime runtime = build_command_runtime(server, backend);

    const CommandResponse listed = runtime.registry->execute(
        caller(ControlSet::User), request("command.list", "{}"));
    BOOST_REQUIRE(listed.ok);
    for (const char* name :
         {"link.create", "link.get", "link.update", "link.delete",
          "link.enable", "link.disable", "link.restore", "link.preview",
          "link.stats", "link.list", "db.tables", "db.rows", "system.info",
          "command.list", "job.get", "job.list", "job.cancel", "link.import",
          "link.export"})
    {
        BOOST_TEST(contains(listed.result_json,
                            std::string(R"("name":")") + name + R"(")"));
    }

    const CommandResponse tables = runtime.registry->execute(
        caller(ControlSet::User), request("db.tables", "{}"));
    BOOST_REQUIRE(tables.ok);
    BOOST_TEST(contains(tables.result_json, R"("name":"links","rows":)"));
    BOOST_TEST(contains(tables.result_json, R"("name":"click_events","rows":)"));
    BOOST_TEST(contains(tables.result_json, R"("name":"jobs","rows":)"));

    reset_process_job_runtime_for_tests();
}

// Type: unit (job submission is audited as acceptance only, T-108 / C6).
// Scenario: with a recording sink, a link.import submission records exactly one
//   job_accepted record whose target is the job id; the following job.get
//   records nothing (it is a non-mutating read).
// Boundaries: only mutating commands audit; a job audits acceptance, not result.
// On failure first check: did the registry stop auditing job acceptance, or
//   start auditing job.get?
BOOST_AUTO_TEST_CASE(import_submission_audits_job_accepted_only)
{
    reset_process_job_runtime_for_tests();
    url_shortener::app::LinkBackend::reset();
    ServerConfig server;
    url_shortener::app::LinkBackendHandle backend;

    CommandRuntimeOptions options;
    auto sink = std::make_shared<RecordingAuditSink>();
    options.audit_sink = sink;
    const CommandRuntime runtime =
        build_command_runtime(server, backend, options);

    const CommandResponse accepted = runtime.registry->execute(
        caller(ControlSet::Admin),
        request("link.import",
                R"({"format":"csv","data":"http://a.com/,jobs64imp\n"})"));
    BOOST_REQUIRE(accepted.job.has_value());
    const std::string job_id = accepted.job->id;

    BOOST_REQUIRE_EQUAL(sink->records.size(), 1u);
    BOOST_TEST(sink->records[0].command == "link.import");
    BOOST_TEST(sink->records[0].outcome == "job_accepted");
    BOOST_TEST(sink->records[0].target == job_id);

    runtime.job_runner->wait_for_terminal(job_id, std::chrono::seconds(20));

    // job.get is a non-mutating read: it audits nothing.
    const CommandResponse got = runtime.registry->execute(
        caller(ControlSet::User),
        request("job.get", R"({"id":")" + job_id + R"("})"));
    BOOST_REQUIRE(got.ok);
    BOOST_TEST(sink->records.size() == 1u);

    reset_process_job_runtime_for_tests();
}

// Type: unit (default-deny file-path capability, T-95).
// Scenario: a runtime from CommandRuntimeOptions{} (as the REST per-request
//   runtime builds it) refuses link.import and link.export with `path` as
//   `unsupported` with no transport-name check, while a runtime built with
//   file_paths.allow_paths = true accepts the `path` (the inline/file factory
//   runs, so a bad path is a path error, not `unsupported`).
// Boundaries: the refusal is driven by the capability alone, not the transport.
// On failure first check: did build_command_runtime start forcing allow_paths,
//   or did the factory add a transport-name gate?
BOOST_AUTO_TEST_CASE(file_paths_default_deny_unless_opted_in)
{
    reset_process_job_runtime_for_tests();
    url_shortener::app::LinkBackend::reset();
    ServerConfig server;
    url_shortener::app::LinkBackendHandle backend;

    // Default options: deny. The same refusal regardless of transport name.
    const CommandRuntime denied = build_command_runtime(server, backend);
    for (const char* transport : {"rest", "cli", "stdio"}) {
        const CommandResponse import_denied = denied.registry->execute(
            caller(ControlSet::Admin, transport),
            request("link.import",
                    R"({"format":"csv","path":"/tmp/jobs64.csv"})"));
        BOOST_TEST(!import_denied.ok);
        BOOST_TEST(
            (import_denied.error.code == CommandErrorCode::unsupported));

        const CommandResponse export_denied = denied.registry->execute(
            caller(ControlSet::Admin, transport),
            request("link.export",
                    R"({"format":"csv","path":"/tmp/jobs64out.csv"})"));
        BOOST_TEST(!export_denied.ok);
        BOOST_TEST(
            (export_denied.error.code == CommandErrorCode::unsupported));
    }

    // Opt in: the factory runs, so a `path` is no longer refused as unsupported.
    // (It may still fail for a path reason, but never `unsupported`.)
    CommandRuntimeOptions opts;
    opts.file_paths.allow_paths = true;
    const CommandRuntime allowed =
        build_command_runtime(server, backend, opts);
    const CommandResponse import_allowed = allowed.registry->execute(
        caller(ControlSet::Admin, "rest"),
        request("link.import",
                R"({"format":"csv","path":"/tmp/jobs64-missing.csv"})"));
    BOOST_TEST((import_allowed.error.code != CommandErrorCode::unsupported));

    reset_process_job_runtime_for_tests();
}

// Type: unit (a `root` jail rejects an out-of-root path as invalid_field).
// Scenario: with allow_paths and a `root` set, link.import with a `path` outside
//   the root is `invalid_field` (not `unsupported`): the capability is granted
//   but the jail rule fails (05.0/07).
// Boundaries: the root jail is a separate rule from the capability gate.
// On failure first check: did the root confinement stop applying, or collapse
//   into the capability refusal?
BOOST_AUTO_TEST_CASE(file_paths_root_confines_path)
{
    reset_process_job_runtime_for_tests();
    url_shortener::app::LinkBackend::reset();
    ServerConfig server;
    url_shortener::app::LinkBackendHandle backend;

    CommandRuntimeOptions opts;
    opts.file_paths.allow_paths = true;
    opts.file_paths.root = "/var/lib/urlshortener-jobs64-root";
    const CommandRuntime runtime =
        build_command_runtime(server, backend, opts);

    const CommandResponse outside = runtime.registry->execute(
        caller(ControlSet::Admin, "cli"),
        request("link.import",
                R"({"format":"csv","path":"/etc/jobs64-escape.csv"})"));
    BOOST_TEST(!outside.ok);
    BOOST_TEST((outside.error.code == CommandErrorCode::invalid_field));

    reset_process_job_runtime_for_tests();
}

// Type: unit (bounded shutdown drains, cancels queued and flags running jobs).
// Scenario: with a single worker, one gated running job and one queued job,
//   shutdown_process_job_runtime(50ms) returns false (drain timed out); the
//   queued job ends cancelled and the running job observes its cancel flag and
//   ends cancelled; a second shutdown returns immediately.
// Boundaries: one worker so the second job stays queued; the running body polls
//   the cancel flag.
// On failure first check: did shutdown stop draining-then-cancelling, or did a
//   second call try to re-join?
BOOST_AUTO_TEST_CASE(shutdown_drains_then_cancels_bounded)
{
    reset_process_job_runtime_for_tests();
    // One worker so the second submission stays queued behind the first.
    JobRuntimeSettings settings;
    settings.runner.worker_count = 1;
    settings.runner.queue_capacity = 8;
    settings.runner.max_retained_jobs = 64;
    BOOST_REQUIRE(configure_process_job_runtime(settings));

    std::shared_ptr<JobRunner> runner = process_job_runner();

    auto running_flag = std::make_shared<std::atomic<bool>>(false);
    JobBody running = [running_flag](IJobContext& ctx) -> JobOutcome
    {
        running_flag->store(true);
        while (!ctx.cancel_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        JobOutcome outcome;
        outcome.ok = false;
        outcome.cancelled = true;  // observed the cancel flag
        return outcome;
    };
    JobBody queued = [](IJobContext& ctx) -> JobOutcome
    {
        // Should never run: it is cancelled while still queued.
        while (!ctx.cancel_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        JobOutcome outcome;
        outcome.ok = false;
        outcome.cancelled = true;
        return outcome;
    };

    const JobSubmitResult first =
        runner->submit("job.a", "d1", std::move(running));
    BOOST_REQUIRE(first.job.has_value());
    const JobSubmitResult second =
        runner->submit("job.b", "d2", std::move(queued));
    BOOST_REQUIRE(second.job.has_value());

    // Wait until the first job is actually running (occupying the one worker).
    while (!running_flag->load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const bool drained =
        shutdown_process_job_runtime(std::chrono::milliseconds(50));
    BOOST_TEST(drained == false);  // the running job did not finish in 50ms

    const std::optional<JobRecord> first_rec = runner->get(first.job->id);
    const std::optional<JobRecord> second_rec = runner->get(second.job->id);
    BOOST_REQUIRE(first_rec.has_value());
    BOOST_REQUIRE(second_rec.has_value());
    BOOST_TEST((first_rec->state == JobState::cancelled));
    BOOST_TEST((second_rec->state == JobState::cancelled));

    // A second shutdown is a no-op and returns immediately.
    BOOST_TEST(shutdown_process_job_runtime(std::chrono::milliseconds(50))
               == true);

    runner.reset();
    reset_process_job_runtime_for_tests();
}

// Type: unit (shutdown of a never-created runner is a true no-op).
// Scenario: with no runner ever created, shutdown_process_job_runtime returns
//   true and creates none; a following process_job_runner() still yields one.
// Boundaries: shutdown must not lazily construct a runner just to drain it.
// On failure first check: did shutdown start force-creating the runner?
BOOST_AUTO_TEST_CASE(shutdown_of_never_created_runner_is_true_noop)
{
    reset_process_job_runtime_for_tests();
    BOOST_TEST(shutdown_process_job_runtime(std::chrono::milliseconds(50))
               == true);
    // The runner is still creatable afterwards.
    BOOST_REQUIRE(process_job_runner() != nullptr);
    reset_process_job_runtime_for_tests();
}

// Type: unit (settings are frozen once the runner is built).
// Scenario: after the runner exists, configure_process_job_runtime returns false
//   and changes nothing (the bounds are fixed at construction).
// Boundaries: configuration is a before-first-use operation.
// On failure first check: did configure stop guarding against a late re-tune?
BOOST_AUTO_TEST_CASE(configure_after_first_use_returns_false)
{
    reset_process_job_runtime_for_tests();
    BOOST_REQUIRE(process_job_runner() != nullptr);  // first use creates it

    JobRuntimeSettings settings;
    settings.runner.worker_count = 4;
    BOOST_TEST(configure_process_job_runtime(settings) == false);

    reset_process_job_runtime_for_tests();
}
