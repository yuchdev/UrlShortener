#define BOOST_TEST_MODULE CommandsOptionsDryRunAsyncTest
#include <atomic>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/arg_spec.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/job_submitter.hpp"

using url_shortener::commands::ArgSpec;
using url_shortener::commands::ArgsView;
using url_shortener::commands::ArgType;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandContext;
using url_shortener::commands::CommandDescriptor;
using url_shortener::commands::CommandError;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandHandler;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::CommandResult;
using url_shortener::commands::ExecutionKind;
using url_shortener::commands::IJobContext;
using url_shortener::commands::IJobSubmitter;
using url_shortener::commands::JobBody;
using url_shortener::commands::JobHandlerFactory;
using url_shortener::commands::JobInfo;
using url_shortener::commands::JobOutcome;
using url_shortener::commands::JobState;
using url_shortener::commands::JobSubmitResult;
using url_shortener::commands::make_error;
using url_shortener::commands::Permission;
using url_shortener::commands::serialize_response;

namespace
{

// Records every submit() call and keeps the moved-in body so a test can run it
// after the originating request has been destroyed. Returns a configurable
// JobSubmitResult. Lives on the test stack; outlives the registry.
struct FakeJobSubmitter : public IJobSubmitter
{
    // Number of times submit() was entered.
    std::atomic<int> calls {0};

    // The command name passed to the last submit().
    std::string last_command;

    // The args_digest passed to the last submit().
    std::string last_args_digest;

    // The body handed to the last submit(); may be run after the request dies.
    JobBody last_body;

    // The result returned from submit(); accepts "j-1"/queued by default.
    JobSubmitResult canned;

    FakeJobSubmitter()
    {
        JobInfo info;
        info.id = "j-1";
        info.state = JobState::queued;
        canned.job = info;
    }

    JobSubmitResult submit(std::string command, std::string args_digest,
                           JobBody body) override
    {
        calls.fetch_add(1);
        last_command = std::move(command);
        last_args_digest = std::move(args_digest);
        last_body = std::move(body);
        return canned;
    }
};

// A submitter whose submit() always throws, to exercise exception isolation.
struct ThrowingJobSubmitter : public IJobSubmitter
{
    JobSubmitResult submit(std::string, std::string, JobBody) override
    {
        throw std::runtime_error("secret-submitter");
    }
};

// Records factory invocations and the observed ctx.dry_run, and builds a body
// that deep-copies the `path` arg so the body survives the request buffer.
struct FakeFactoryRecorder
{
    // Number of times the factory was entered.
    std::atomic<int> calls {0};

    // The ctx.dry_run value seen by the last factory call.
    std::atomic<bool> last_dry_run {false};

    // When true, the factory returns a CommandError instead of a body.
    bool return_error = false;

    // The error code returned when `return_error` is set.
    CommandErrorCode error_code = CommandErrorCode::invalid_field;

    // When true, the factory throws instead of returning.
    bool throw_in_factory = false;

    JobHandlerFactory factory()
    {
        return [this](const CommandContext& ctx, const ArgsView& view)
                   -> std::variant<JobBody, CommandError> {
            calls.fetch_add(1);
            last_dry_run.store(ctx.dry_run);
            if (throw_in_factory)
            {
                throw std::runtime_error("secret-factory");
            }
            if (return_error)
            {
                return make_error(error_code, "factory failed");
            }
            // Deep-copy the arg into the body so it is self-contained and holds
            // no reference to request-owned memory (threat model T-12).
            const std::string copied =
                view.get_string("path").value_or("none");
            return JobBody {[copied](IJobContext&) {
                JobOutcome out;
                out.ok = true;
                out.result_json = std::string(R"({"copied":")") + copied + "\"}";
                return out;
            }};
        };
    }
};

// Records handler invocations, the observed ctx.dry_run, and increments a
// mutation counter only when NOT running under dry_run (proving the flag
// reaches the handler).
struct DryRunHandlerRecorder
{
    // Number of times the handler was entered.
    std::atomic<int> calls {0};

    // The ctx.dry_run value seen by the last handler call.
    std::atomic<bool> last_dry_run {false};

    // Incremented only on a real (non-dry-run) call.
    std::atomic<int> mutations {0};

    CommandHandler handler()
    {
        return [this](CommandContext& ctx, const ArgsView&) -> CommandResult {
            calls.fetch_add(1);
            last_dry_run.store(ctx.dry_run);
            if (!ctx.dry_run)
            {
                mutations.fetch_add(1);
            }
            CommandResult r;
            r.result_json = R"({"ok":true})";
            return r;
        };
    }
};

// Builds a sync descriptor that supports dry_run (mutating + write).
CommandDescriptor dry_run_sync_descriptor(std::string name,
                                          CommandHandler handler)
{
    CommandDescriptor d;
    d.name = std::move(name);
    d.summary = "a summary";
    d.permission = Permission::write;
    d.mutating = true;
    d.supports_dry_run = true;
    d.handler = std::move(handler);
    return d;
}

// Builds a job descriptor (kind == job). A `path` string arg lets the factory
// copy a value into the body.
CommandDescriptor job_descriptor(std::string name, JobHandlerFactory factory)
{
    CommandDescriptor d;
    d.name = std::move(name);
    d.summary = "a summary";
    d.kind = ExecutionKind::job;
    d.job_factory = std::move(factory);
    ArgSpec path;
    path.name = "path";
    path.type = ArgType::string;
    d.args = {path};
    return d;
}

// Builds a request for a command with the given args JSON text.
CommandRequest request_for(std::string command, std::string args_json = "{}")
{
    CommandRequest request;
    request.id = "client-1";
    request.command = std::move(command);
    request.args_json = std::move(args_json);
    return request;
}

}  // namespace

// ---------------------------------------------------------------------------
// Row 1: dry_run on a command that does not support it -> unsupported.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(dry_run_not_supported_is_unsupported)
{
    DryRunHandlerRecorder recorder;
    CommandRegistry registry;

    // A plain read command: not supports_dry_run.
    CommandDescriptor d;
    d.name = "link.get";
    d.summary = "a summary";
    d.permission = Permission::read;
    d.handler = recorder.handler();
    registry.add(std::move(d));

    CallerContext caller;
    CommandRequest req = request_for("link.get");
    req.options.dry_run = true;
    const CommandResponse resp = registry.execute(caller, req);

    BOOST_TEST(resp.ok == false);
    BOOST_TEST((resp.error.code == CommandErrorCode::unsupported));
    BOOST_TEST(resp.error.message == "dry_run not supported");
    BOOST_TEST(recorder.calls.load() == 0);
}

// ---------------------------------------------------------------------------
// Row 2: dry_run together with async is mutually exclusive -> unsupported,
// even on a command that supports dry_run.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(dry_run_and_async_are_mutually_exclusive)
{
    DryRunHandlerRecorder recorder;
    CommandRegistry registry;
    registry.add(dry_run_sync_descriptor("link.delete", recorder.handler()));

    CallerContext caller;
    caller.control_set = ControlSet::Admin;
    CommandRequest req = request_for("link.delete");
    req.options.dry_run = true;
    req.options.async = true;
    const CommandResponse resp = registry.execute(caller, req);

    BOOST_TEST(resp.ok == false);
    BOOST_TEST((resp.error.code == CommandErrorCode::unsupported));
    BOOST_TEST(resp.error.message == "dry_run and async are mutually exclusive");
    BOOST_TEST(recorder.calls.load() == 0);
}

// ---------------------------------------------------------------------------
// Row 3: dry_run on a supporting sync command -> handler runs with
// ctx.dry_run == true, success envelope, and the handler's own dry-run branch
// left the mutation counter unchanged (the flag reached it).
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(dry_run_sync_handler_sees_flag_and_does_not_mutate)
{
    DryRunHandlerRecorder recorder;
    CommandRegistry registry;
    registry.add(dry_run_sync_descriptor("link.delete", recorder.handler()));

    CallerContext caller;
    caller.control_set = ControlSet::Admin;
    CommandRequest req = request_for("link.delete");
    req.options.dry_run = true;
    const CommandResponse resp = registry.execute(caller, req);

    BOOST_TEST(resp.ok == true);
    BOOST_TEST(resp.result_json == R"({"ok":true})");
    BOOST_TEST(resp.job.has_value() == false);
    BOOST_TEST(recorder.calls.load() == 1);
    BOOST_TEST(recorder.last_dry_run.load() == true);
    // The handler's dry-run branch performed no mutation.
    BOOST_TEST(recorder.mutations.load() == 0);
}

// ---------------------------------------------------------------------------
// Row 4: dry_run on a kind==job command -> the job_factory runs with
// ctx.dry_run == true and its body runs synchronously; a result is returned,
// no job is created, and no submitter is involved.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(dry_run_job_runs_factory_body_synchronously_no_job)
{
    FakeFactoryRecorder factory;
    // Optional for a job command; present only to prove it is not the dry_run
    // path.
    DryRunHandlerRecorder handler;
    auto submitter = std::make_shared<FakeJobSubmitter>();

    CommandRegistry registry;
    registry.set_job_submitter(submitter);

    // A supports_dry_run job command needs a factory (kind == job); its
    // dry_run path runs the factory body, never the handler (spec row 4,
    // T-21).
    CommandDescriptor d = job_descriptor("link.export", factory.factory());
    d.permission = Permission::write;
    d.mutating = true;
    d.supports_dry_run = true;
    d.handler = handler.handler();
    registry.add(std::move(d));

    CallerContext caller;
    caller.control_set = ControlSet::Admin;
    CommandRequest req = request_for("link.export", R"({"path":"/tmp/out"})");
    req.options.dry_run = true;
    const CommandResponse resp = registry.execute(caller, req);

    BOOST_TEST(resp.ok == true);
    BOOST_TEST(resp.job.has_value() == false);
    BOOST_TEST(resp.result_json.find("/tmp/out") != std::string::npos);
    BOOST_TEST(factory.calls.load() == 1);
    BOOST_TEST(factory.last_dry_run.load() == true);
    // No job was created and the handler was not the dry_run path.
    BOOST_TEST(submitter->calls.load() == 0);
    BOOST_TEST(handler.calls.load() == 0);
}

// ---------------------------------------------------------------------------
// Row 5: async on a non-async-capable sync command -> unsupported.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(async_not_supported_on_sync_command)
{
    DryRunHandlerRecorder recorder;
    auto submitter = std::make_shared<FakeJobSubmitter>();
    CommandRegistry registry;
    registry.set_job_submitter(submitter);

    CommandDescriptor d;
    d.name = "link.get";
    d.summary = "a summary";
    d.permission = Permission::read;
    d.handler = recorder.handler();
    registry.add(std::move(d));

    CallerContext caller;
    CommandRequest req = request_for("link.get");
    req.options.async = true;
    const CommandResponse resp = registry.execute(caller, req);

    BOOST_TEST(resp.ok == false);
    BOOST_TEST((resp.error.code == CommandErrorCode::unsupported));
    BOOST_TEST(resp.error.message == "async not supported");
    BOOST_TEST(recorder.calls.load() == 0);
    BOOST_TEST(submitter->calls.load() == 0);
}

// ---------------------------------------------------------------------------
// Row 6: a kind==job command with a submitter installed -> job accepted. The
// response is ok with job.id/state == queued, result is absent, and the
// serialized envelope matches the catalog job example shape.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(job_command_is_accepted_through_submitter)
{
    FakeFactoryRecorder factory;
    auto submitter = std::make_shared<FakeJobSubmitter>();
    JobInfo info;
    info.id = "j-9";
    info.state = JobState::queued;
    submitter->canned.job = info;

    CommandRegistry registry;
    registry.set_job_submitter(submitter);
    registry.add(job_descriptor("link.export", factory.factory()));

    CallerContext caller;
    const CommandResponse resp = registry.execute(
        caller, request_for("link.export", R"({"path":"/tmp/out"})"));

    BOOST_TEST(resp.ok == true);
    BOOST_TEST(resp.job.has_value() == true);
    BOOST_TEST(resp.job->id == "j-9");
    BOOST_TEST((resp.job->state == JobState::queued));
    BOOST_TEST(resp.result_json.empty() == true);
    BOOST_TEST(factory.calls.load() == 1);
    BOOST_TEST(factory.last_dry_run.load() == false);
    BOOST_TEST(submitter->calls.load() == 1);
    BOOST_TEST(submitter->last_command == "link.export");

    // Serialized envelope carries the job shape, not a result.
    const std::string wire = serialize_response(resp);
    BOOST_TEST(wire.find(R"("job":)") != std::string::npos);
    BOOST_TEST(wire.find(R"("id":"j-9")") != std::string::npos);
    BOOST_TEST(wire.find(R"("state":"queued")") != std::string::npos);
    BOOST_TEST(wire.find(R"("result":)") == std::string::npos);
}

// ---------------------------------------------------------------------------
// Row 6 (async on async_capable sync command): async runs it as a job through
// the submitter, using the job_factory.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(async_capable_sync_command_runs_as_job)
{
    FakeFactoryRecorder factory;
    DryRunHandlerRecorder handler;
    auto submitter = std::make_shared<FakeJobSubmitter>();

    CommandRegistry registry;
    registry.set_job_submitter(submitter);

    CommandDescriptor d;
    d.name = "link.get";
    d.summary = "a summary";
    d.permission = Permission::read;
    d.async_capable = true;  // requires a job_factory even for kind == sync
    d.handler = handler.handler();
    d.job_factory = factory.factory();
    ArgSpec path;
    path.name = "path";
    path.type = ArgType::string;
    d.args = {path};
    registry.add(std::move(d));

    CallerContext caller;
    CommandRequest req = request_for("link.get", R"({"path":"/tmp/out"})");
    req.options.async = true;
    const CommandResponse resp = registry.execute(caller, req);

    BOOST_TEST(resp.ok == true);
    BOOST_TEST(resp.job.has_value() == true);
    BOOST_TEST(factory.calls.load() == 1);
    BOOST_TEST(submitter->calls.load() == 1);
    // The sync handler is not the async path.
    BOOST_TEST(handler.calls.load() == 0);
}

// ---------------------------------------------------------------------------
// A factory that returns a CommandError -> failure envelope, submitter NOT
// called (nothing queued).
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(factory_error_is_failure_and_nothing_queued)
{
    FakeFactoryRecorder factory;
    factory.return_error = true;
    factory.error_code = CommandErrorCode::invalid_url;
    auto submitter = std::make_shared<FakeJobSubmitter>();

    CommandRegistry registry;
    registry.set_job_submitter(submitter);
    registry.add(job_descriptor("link.export", factory.factory()));

    CallerContext caller;
    const CommandResponse resp = registry.execute(
        caller, request_for("link.export", R"({"path":"/tmp/out"})"));

    BOOST_TEST(resp.ok == false);
    BOOST_TEST((resp.error.code == CommandErrorCode::invalid_url));
    BOOST_TEST(factory.calls.load() == 1);
    BOOST_TEST(submitter->calls.load() == 0);
}

// ---------------------------------------------------------------------------
// Args are copied: destroying the request buffer after execute does not affect
// the body the submitter received (threat model T-12).
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(body_is_self_contained_after_request_destroyed)
{
    FakeFactoryRecorder factory;
    auto submitter = std::make_shared<FakeJobSubmitter>();

    CommandRegistry registry;
    registry.set_job_submitter(submitter);
    registry.add(job_descriptor("link.export", factory.factory()));

    {
        // The request and its args buffer live only in this scope.
        auto request = std::make_unique<CommandRequest>(
            request_for("link.export", R"({"path":"/tmp/secret-path"})"));
        CallerContext caller;
        const CommandResponse resp = registry.execute(caller, *request);
        BOOST_TEST(resp.ok == true);
        BOOST_TEST(submitter->calls.load() == 1);
        // Destroy the request/ArgsView-source buffer before running the body.
        request.reset();
    }

    // Running the captured body now must still produce the copied value.
    BOOST_TEST(static_cast<bool>(submitter->last_body) == true);
    struct NoopCtx : public IJobContext
    {
        bool cancel_requested() const noexcept override { return false; }
        void set_total(std::uint64_t) override {}
        void advance(std::uint64_t = 1) override {}
    } ctx;
    const JobOutcome outcome = submitter->last_body(ctx);
    BOOST_TEST(outcome.ok == true);
    BOOST_TEST(outcome.result_json.has_value() == true);
    BOOST_TEST(outcome.result_json->find("/tmp/secret-path") !=
               std::string::npos);
}

// ---------------------------------------------------------------------------
// args_digest is 16 hex chars and equal for equal args.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(args_digest_is_16_hex_and_stable)
{
    FakeFactoryRecorder factory;
    auto submitter = std::make_shared<FakeJobSubmitter>();

    CommandRegistry registry;
    registry.set_job_submitter(submitter);
    registry.add(job_descriptor("link.export", factory.factory()));

    CallerContext caller;
    const CommandResponse a = registry.execute(
        caller, request_for("link.export", R"({"path":"/tmp/out"})"));
    BOOST_TEST(a.ok == true);
    const std::string digest_a = submitter->last_args_digest;

    const CommandResponse b = registry.execute(
        caller, request_for("link.export", R"({"path":"/tmp/out"})"));
    BOOST_TEST(b.ok == true);
    const std::string digest_b = submitter->last_args_digest;

    // 16 lower-case hex characters.
    BOOST_TEST(digest_a.size() == 16u);
    const bool all_hex = digest_a.find_first_not_of("0123456789abcdef") ==
                         std::string::npos;
    BOOST_TEST(all_hex == true);
    // Equal args -> equal digest.
    BOOST_TEST(digest_a == digest_b);

    // Different args -> (almost certainly) different digest.
    const CommandResponse c = registry.execute(
        caller, request_for("link.export", R"({"path":"/tmp/other"})"));
    BOOST_TEST(c.ok == true);
    BOOST_TEST(submitter->last_args_digest != digest_a);
}

// ---------------------------------------------------------------------------
// The submitter rejecting with busy / internal surfaces as a failure envelope
// with that code (HTTP 503 / exit 4 for busy, via the subtask-02 table).
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(submitter_rejection_surfaces_as_failure)
{
    for (const CommandErrorCode code :
         {CommandErrorCode::busy, CommandErrorCode::internal})
    {
        FakeFactoryRecorder factory;
        auto submitter = std::make_shared<FakeJobSubmitter>();
        submitter->canned.job = std::nullopt;
        submitter->canned.error = make_error(code, "rejected");

        CommandRegistry registry;
        registry.set_job_submitter(submitter);
        registry.add(job_descriptor("link.export", factory.factory()));

        CallerContext caller;
        const CommandResponse resp = registry.execute(
            caller, request_for("link.export", R"({"path":"/tmp/out"})"));

        BOOST_TEST(resp.ok == false);
        BOOST_TEST((resp.error.code == code));
        BOOST_TEST(submitter->calls.load() == 1);
    }
}

// ---------------------------------------------------------------------------
// No submitter installed + job descriptor -> unsupported (fail closed) and the
// factory is never called (threat model T-12).
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(no_submitter_job_is_unsupported_factory_not_called)
{
    FakeFactoryRecorder factory;
    CommandRegistry registry;  // no submitter installed
    registry.add(job_descriptor("link.export", factory.factory()));

    CallerContext caller;
    const CommandResponse resp = registry.execute(
        caller, request_for("link.export", R"({"path":"/tmp/out"})"));

    BOOST_TEST(resp.ok == false);
    BOOST_TEST((resp.error.code == CommandErrorCode::unsupported));
    BOOST_TEST(factory.calls.load() == 0);
}

// ---------------------------------------------------------------------------
// Authorization runs before the factory/submitter: an unauthorized async
// request -> not_authorized, neither factory nor submitter called.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(unauthorized_async_denied_before_factory)
{
    FakeFactoryRecorder factory;
    DryRunHandlerRecorder handler;
    auto submitter = std::make_shared<FakeJobSubmitter>();

    CommandRegistry registry;
    registry.set_job_submitter(submitter);

    // A write-permission async_capable sync command.
    CommandDescriptor d;
    d.name = "link.delete";
    d.summary = "a summary";
    d.permission = Permission::write;
    d.mutating = true;
    d.async_capable = true;
    d.handler = handler.handler();
    d.job_factory = factory.factory();
    registry.add(std::move(d));

    CallerContext caller;
    caller.control_set = ControlSet::User;  // lacks write
    CommandRequest req = request_for("link.delete");
    req.options.async = true;
    const CommandResponse resp = registry.execute(caller, req);

    BOOST_TEST(resp.ok == false);
    BOOST_TEST((resp.error.code == CommandErrorCode::not_authorized));
    BOOST_TEST(factory.calls.load() == 0);
    BOOST_TEST(submitter->calls.load() == 0);
}

// ---------------------------------------------------------------------------
// Validation runs before the factory/submitter: an invalid-args async request
// -> invalid_field, neither factory nor submitter called.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(invalid_args_rejected_before_factory)
{
    FakeFactoryRecorder factory;
    auto submitter = std::make_shared<FakeJobSubmitter>();

    CommandRegistry registry;
    registry.set_job_submitter(submitter);
    registry.add(job_descriptor("link.export", factory.factory()));

    CallerContext caller;
    // `bogus` is not a declared arg -> invalid_field before dispatch.
    const CommandResponse resp = registry.execute(
        caller, request_for("link.export", R"({"bogus":"x"})"));

    BOOST_TEST(resp.ok == false);
    BOOST_TEST((resp.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(factory.calls.load() == 0);
    BOOST_TEST(submitter->calls.load() == 0);
}

// ---------------------------------------------------------------------------
// set_job_submitter(nullptr) clears the seam: a previously-working job
// descriptor then fails closed with unsupported.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(set_job_submitter_nullptr_clears_seam)
{
    FakeFactoryRecorder factory;
    auto submitter = std::make_shared<FakeJobSubmitter>();

    CommandRegistry registry;
    registry.set_job_submitter(submitter);
    registry.add(job_descriptor("link.export", factory.factory()));

    CallerContext caller;
    const CommandResponse accepted = registry.execute(
        caller, request_for("link.export", R"({"path":"/tmp/out"})"));
    BOOST_TEST(accepted.ok == true);

    // Clear the seam.
    registry.set_job_submitter(nullptr);
    const int factory_calls_before = factory.calls.load();
    const CommandResponse closed = registry.execute(
        caller, request_for("link.export", R"({"path":"/tmp/out"})"));
    BOOST_TEST(closed.ok == false);
    BOOST_TEST((closed.error.code == CommandErrorCode::unsupported));
    // The factory is not invoked once the seam is cleared.
    BOOST_TEST(factory.calls.load() == factory_calls_before);
}

// ---------------------------------------------------------------------------
// An exception escaping the factory -> internal, with no exception text leaked
// (same isolation as handlers, threat model T-10).
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(factory_exception_is_internal_and_leaks_nothing)
{
    FakeFactoryRecorder factory;
    factory.throw_in_factory = true;
    auto submitter = std::make_shared<FakeJobSubmitter>();

    CommandRegistry registry;
    registry.set_job_submitter(submitter);
    registry.add(job_descriptor("link.export", factory.factory()));

    CallerContext caller;
    const CommandResponse resp = registry.execute(
        caller, request_for("link.export", R"({"path":"/tmp/out"})"));

    BOOST_TEST(resp.ok == false);
    BOOST_TEST((resp.error.code == CommandErrorCode::internal));
    BOOST_TEST(resp.error.message == "internal error");
    BOOST_TEST(serialize_response(resp).find("secret-factory") ==
               std::string::npos);
    BOOST_TEST(submitter->calls.load() == 0);
}

// ---------------------------------------------------------------------------
// An exception escaping the submitter -> internal, with no exception text
// leaked.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(submitter_exception_is_internal_and_leaks_nothing)
{
    FakeFactoryRecorder factory;
    auto submitter = std::make_shared<ThrowingJobSubmitter>();

    CommandRegistry registry;
    registry.set_job_submitter(submitter);
    registry.add(job_descriptor("link.export", factory.factory()));

    CallerContext caller;
    const CommandResponse resp = registry.execute(
        caller, request_for("link.export", R"({"path":"/tmp/out"})"));

    BOOST_TEST(resp.ok == false);
    BOOST_TEST((resp.error.code == CommandErrorCode::internal));
    BOOST_TEST(resp.error.message == "internal error");
    BOOST_TEST(serialize_response(resp).find("secret-submitter") ==
               std::string::npos);
    // The factory did build a body before submit threw.
    BOOST_TEST(factory.calls.load() == 1);
}
