#define BOOST_TEST_MODULE CommandsExecutePipelineTest
#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/arg_spec.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"

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
using url_shortener::commands::JobBody;
using url_shortener::commands::JobHandlerFactory;
using url_shortener::commands::JobOutcome;
using url_shortener::commands::make_error;
using url_shortener::commands::Permission;
using url_shortener::commands::serialize_response;

namespace
{

// Counts handler invocations and returns a canned result. Lives on the stack
// of each test and outlives the registry built within that test.
struct FakeHandlerRecorder
{
    // Number of times the handler was entered; atomic for the threading smoke
    // test.
    std::atomic<int> calls {0};

    // The result the handler returns on every call.
    CommandResult canned;

    FakeHandlerRecorder() { canned.result_json = R"({"ok":true})"; }

    // Returns a handler that records the call and returns `canned`.
    CommandHandler handler()
    {
        return [this](CommandContext&, const ArgsView&) -> CommandResult
        {
            calls.fetch_add(1);
            return canned;
        };
    }
};

// A job factory that is never meaningfully invoked in these tests.
JobHandlerFactory noop_job_factory()
{
    return [](const CommandContext&,
              const ArgsView&) -> std::variant<JobBody, CommandError>
    {
        return JobBody {[](url_shortener::commands::IJobContext&)
                        { return JobOutcome {}; }};
    };
}

// Builds a sync descriptor with the given name, permission and handler.
CommandDescriptor sync_descriptor(std::string name,
                                  Permission permission,
                                  CommandHandler handler)
{
    CommandDescriptor descriptor;
    descriptor.name = std::move(name);
    descriptor.summary = "a summary";
    descriptor.permission = permission;
    descriptor.mutating = permission != Permission::read;
    descriptor.handler = std::move(handler);
    return descriptor;
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
// Happy path: a read command succeeds for Admin and User; the response echoes
// the id and carries the handler's result_json verbatim.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(happy_path_read_admin_and_user)
{
    FakeHandlerRecorder recorder;
    CommandRegistry registry;
    registry.add(
        sync_descriptor("link.get", Permission::read, recorder.handler()));

    for (const ControlSet role : {ControlSet::Admin, ControlSet::User}) {
        CallerContext caller;
        caller.control_set = role;
        const CommandResponse response =
            registry.execute(caller, request_for("link.get"));
        BOOST_TEST(response.ok == true);
        BOOST_TEST(response.id == "client-1");
        BOOST_TEST(response.result_json == R"({"ok":true})");
    }
    BOOST_TEST(recorder.calls.load() == 2);
}

// ---------------------------------------------------------------------------
// Unknown command: unknown_command, and the submitted name is not echoed.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(unknown_command_does_not_echo_name)
{
    CommandRegistry registry;
    CallerContext caller;

    const CommandResponse response =
        registry.execute(caller, request_for("secret.command"));
    BOOST_TEST(response.ok == false);
    BOOST_TEST((response.error.code == CommandErrorCode::unknown_command));
    BOOST_TEST(response.error.message.find("secret") == std::string::npos);

    const std::string wire = serialize_response(response);
    BOOST_TEST(wire.find("secret") == std::string::npos);
}

// ---------------------------------------------------------------------------
// Authorization matrix: read -> Admin+User ok; write/migrate/manage_users ->
// Admin ok, User not_authorized with the handler never entered.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(authorization_matrix_handler_not_entered_on_denial)
{
    const Permission permissions[] = {Permission::read,
                                      Permission::write,
                                      Permission::migrate,
                                      Permission::manage_users};

    for (const Permission permission : permissions) {
        for (const ControlSet role : {ControlSet::Admin, ControlSet::User}) {
            FakeHandlerRecorder recorder;
            CommandRegistry registry;
            registry.add(
                sync_descriptor("ns.cmd", permission, recorder.handler()));

            CallerContext caller;
            caller.control_set = role;
            const CommandResponse response =
                registry.execute(caller, request_for("ns.cmd"));

            const bool should_pass =
                permission == Permission::read || role == ControlSet::Admin;
            if (should_pass) {
                BOOST_TEST(response.ok == true);
                BOOST_TEST(recorder.calls.load() == 1);
            }
            else {
                BOOST_TEST(response.ok == false);
                BOOST_TEST(
                    (response.error.code == CommandErrorCode::not_authorized));
                BOOST_TEST(response.error.message == "permission denied");
                // The authorization gate must precede dispatch (T-08).
                BOOST_TEST(recorder.calls.load() == 0);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Denial precedes validation: an unauthorized caller with invalid args gets
// not_authorized, not invalid_field.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(denial_precedes_validation)
{
    FakeHandlerRecorder recorder;
    CommandRegistry registry;

    CommandDescriptor descriptor =
        sync_descriptor("link.delete", Permission::write, recorder.handler());
    ArgSpec slug;
    slug.name = "slug";
    slug.type = ArgType::string;
    slug.required = true;
    descriptor.args = {slug};
    registry.add(std::move(descriptor));

    CallerContext caller;
    caller.control_set = ControlSet::User;
    // Invalid args (required `slug` missing, plus an unknown key) must not be
    // reported: authorization fails first.
    const CommandResponse response = registry.execute(
        caller, request_for("link.delete", R"({"bogus":"x"})"));

    BOOST_TEST(response.ok == false);
    BOOST_TEST((response.error.code == CommandErrorCode::not_authorized));
    BOOST_TEST(recorder.calls.load() == 0);
}

// ---------------------------------------------------------------------------
// Validation errors: unknown key, missing required and wrong type each yield
// invalid_field, the handler is not entered, and the message names the arg but
// never the value.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(validation_errors_name_arg_not_value)
{
    FakeHandlerRecorder recorder;
    CommandRegistry registry;

    CommandDescriptor descriptor =
        sync_descriptor("link.get", Permission::read, recorder.handler());
    ArgSpec slug;
    slug.name = "slug";
    slug.type = ArgType::string;
    slug.required = true;
    descriptor.args = {slug};
    registry.add(std::move(descriptor));

    CallerContext caller;

    // Unknown key: message names `bogus`, never the secret value.
    const CommandResponse unknown = registry.execute(
        caller, request_for("link.get", R"({"slug":"a","bogus":"topsecret"})"));
    BOOST_TEST(unknown.ok == false);
    BOOST_TEST((unknown.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(unknown.error.message.find("bogus") != std::string::npos);
    BOOST_TEST(unknown.error.message.find("topsecret") == std::string::npos);

    // Missing required.
    const CommandResponse missing =
        registry.execute(caller, request_for("link.get", R"({})"));
    BOOST_TEST(missing.ok == false);
    BOOST_TEST((missing.error.code == CommandErrorCode::invalid_field));

    // Wrong type (integer where a string is required).
    const CommandResponse wrong_type =
        registry.execute(caller, request_for("link.get", R"({"slug":123})"));
    BOOST_TEST(wrong_type.ok == false);
    BOOST_TEST((wrong_type.error.code == CommandErrorCode::invalid_field));

    BOOST_TEST(recorder.calls.load() == 0);
}

// ---------------------------------------------------------------------------
// permission_for (D16): a read descriptor whose hook raises to write when
// `path` is present. User without path -> ok; User with path -> not_authorized
// and handler not entered; Admin with path -> ok.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(permission_for_raises_on_path)
{
    FakeHandlerRecorder recorder;
    CommandRegistry registry;

    CommandDescriptor descriptor =
        sync_descriptor("link.export", Permission::read, recorder.handler());
    // A mutating read-base command is only valid with a raise hook (D16).
    descriptor.mutating = true;
    ArgSpec path;
    path.name = "path";
    path.type = ArgType::string;
    descriptor.args = {path};
    descriptor.permission_for = [](const ArgsView& view)
    { return view.has("path") ? Permission::write : Permission::read; };
    registry.add(std::move(descriptor));

    // User, no path -> read suffices.
    {
        CallerContext caller;
        caller.control_set = ControlSet::User;
        const CommandResponse response =
            registry.execute(caller, request_for("link.export", R"({})"));
        BOOST_TEST(response.ok == true);
    }
    // User, path present -> raised to write -> not_authorized, handler not run.
    {
        const int before = recorder.calls.load();
        CallerContext caller;
        caller.control_set = ControlSet::User;
        const CommandResponse response = registry.execute(
            caller, request_for("link.export", R"({"path":"/tmp/out"})"));
        BOOST_TEST(response.ok == false);
        BOOST_TEST((response.error.code == CommandErrorCode::not_authorized));
        BOOST_TEST(recorder.calls.load() == before);
    }
    // Admin, path present -> write granted.
    {
        CallerContext caller;
        caller.control_set = ControlSet::Admin;
        const CommandResponse response = registry.execute(
            caller, request_for("link.export", R"({"path":"/tmp/out"})"));
        BOOST_TEST(response.ok == true);
    }
}

// ---------------------------------------------------------------------------
// permission_for: invalid args produce invalid_field BEFORE the hook runs, so
// the hook never sees an unvalidated view.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(permission_for_not_run_on_invalid_args)
{
    FakeHandlerRecorder recorder;
    CommandRegistry registry;
    std::atomic<int> hook_calls {0};

    CommandDescriptor descriptor =
        sync_descriptor("link.export", Permission::read, recorder.handler());
    descriptor.mutating = true;
    ArgSpec path;
    path.name = "path";
    path.type = ArgType::string;
    descriptor.args = {path};
    descriptor.permission_for = [&hook_calls](const ArgsView&)
    {
        hook_calls.fetch_add(1);
        return Permission::write;
    };
    registry.add(std::move(descriptor));

    CallerContext caller;
    caller.control_set = ControlSet::Admin;
    const CommandResponse response = registry.execute(
        caller, request_for("link.export", R"({"unknown":"x"})"));
    BOOST_TEST(response.ok == false);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(hook_calls.load() == 0);
    BOOST_TEST(recorder.calls.load() == 0);
}

// ---------------------------------------------------------------------------
// permission_for: a hook that throws maps to internal with a fixed message.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(permission_for_throwing_hook_is_internal)
{
    FakeHandlerRecorder recorder;
    CommandRegistry registry;

    CommandDescriptor descriptor =
        sync_descriptor("link.export", Permission::read, recorder.handler());
    descriptor.mutating = true;
    descriptor.permission_for = [](const ArgsView&) -> Permission
    { throw std::runtime_error("secret-dsn"); };
    registry.add(std::move(descriptor));

    CallerContext caller;
    caller.control_set = ControlSet::Admin;
    const CommandResponse response =
        registry.execute(caller, request_for("link.export", R"({})"));
    BOOST_TEST(response.ok == false);
    BOOST_TEST((response.error.code == CommandErrorCode::internal));
    BOOST_TEST(response.error.message == "internal error");
    BOOST_TEST(serialize_response(response).find("secret-dsn")
               == std::string::npos);
    BOOST_TEST(recorder.calls.load() == 0);
}

// Scenario: an unmapped Permission value (only producible by a cast, e.g. a
// hook returning a value outside the enum) must fail closed, even for Admin
// (security review LOW on 01.0/07: the mapping switch must never grant by
// falling through).
BOOST_AUTO_TEST_CASE(unmapped_permission_fails_closed)
{
    FakeHandlerRecorder recorder;
    CommandRegistry registry;

    CommandDescriptor descriptor =
        sync_descriptor("link.purge", Permission::write, recorder.handler());
    descriptor.permission_for = [](const ArgsView&)
    { return static_cast<Permission>(99); };
    registry.add(std::move(descriptor));

    CallerContext caller;
    caller.control_set = ControlSet::Admin;
    const CommandResponse response =
        registry.execute(caller, request_for("link.purge"));
    BOOST_TEST(response.ok == false);
    BOOST_TEST((response.error.code == CommandErrorCode::not_authorized));
    BOOST_TEST(recorder.calls.load() == 0);
}

// ---------------------------------------------------------------------------
// permission_for carry-forward (T-09): a write descriptor whose hook returns a
// LOWER permission (read) still requires write (tighten-only). A User is
// denied at the base check; the hook can never lower the floor.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(permission_for_cannot_lower_base)
{
    FakeHandlerRecorder recorder;
    CommandRegistry registry;

    CommandDescriptor descriptor =
        sync_descriptor("link.delete", Permission::write, recorder.handler());
    descriptor.permission_for = [](const ArgsView&)
    { return Permission::read; };
    registry.add(std::move(descriptor));

    // User: base write already denies, regardless of the hook's read result.
    {
        CallerContext caller;
        caller.control_set = ControlSet::User;
        const CommandResponse response =
            registry.execute(caller, request_for("link.delete"));
        BOOST_TEST(response.ok == false);
        BOOST_TEST((response.error.code == CommandErrorCode::not_authorized));
        BOOST_TEST(recorder.calls.load() == 0);
    }
    // Admin: write granted.
    {
        CallerContext caller;
        caller.control_set = ControlSet::Admin;
        const CommandResponse response =
            registry.execute(caller, request_for("link.delete"));
        BOOST_TEST(response.ok == true);
    }
}

// ---------------------------------------------------------------------------
// Handler-reported expected failures pass through with the same code and the
// handler's message preserved.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(handler_expected_failures_pass_through)
{
    for (const CommandErrorCode code :
         {CommandErrorCode::not_found, CommandErrorCode::slug_conflict})
    {
        FakeHandlerRecorder recorder;
        recorder.canned.result_json.clear();
        recorder.canned.error = make_error(code, "handler message");

        CommandRegistry registry;
        registry.add(
            sync_descriptor("link.get", Permission::read, recorder.handler()));

        CallerContext caller;
        const CommandResponse response =
            registry.execute(caller, request_for("link.get"));
        BOOST_TEST(response.ok == false);
        BOOST_TEST((response.error.code == code));
        BOOST_TEST(response.error.message == "handler message");
    }
}

// ---------------------------------------------------------------------------
// A throwing handler maps to internal with a fixed message; its exception text
// never reaches the serialized response.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(throwing_handler_is_internal_and_leaks_nothing)
{
    CommandRegistry registry;

    registry.add(
        sync_descriptor("ns.throws_std",
                        Permission::read,
                        [](CommandContext&, const ArgsView&) -> CommandResult
                        { throw std::runtime_error("secret-dsn"); }));
    registry.add(sync_descriptor(
        "ns.throws_int",
        Permission::read,
        [](CommandContext&, const ArgsView&) -> CommandResult { throw 42; }));

    CallerContext caller;
    for (const char* name : {"ns.throws_std", "ns.throws_int"}) {
        const CommandResponse response =
            registry.execute(caller, request_for(name));
        BOOST_TEST(response.ok == false);
        BOOST_TEST((response.error.code == CommandErrorCode::internal));
        BOOST_TEST(response.error.message == "internal error");
        BOOST_TEST(serialize_response(response).find("secret-dsn")
                   == std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// A successful handler that returns empty result_json is a programming error
// mapped to internal.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(empty_result_json_is_internal)
{
    FakeHandlerRecorder recorder;
    recorder.canned.result_json.clear();

    CommandRegistry registry;
    registry.add(
        sync_descriptor("link.get", Permission::read, recorder.handler()));

    CallerContext caller;
    const CommandResponse response =
        registry.execute(caller, request_for("link.get"));
    BOOST_TEST(response.ok == false);
    BOOST_TEST((response.error.code == CommandErrorCode::internal));
    BOOST_TEST(recorder.calls.load() == 1);
}

// ---------------------------------------------------------------------------
// With no submitter installed, dry_run on a non-supporting command, async on a
// non-async-capable sync command, and a job descriptor all fail closed with
// `unsupported`, and the sync handler is never entered. The full options/job
// table (dispatch, dry_run success, job acceptance, args_digest, exception
// isolation) is covered in 08_options_dry_run_async.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(options_and_job_are_unsupported)
{
    FakeHandlerRecorder recorder;
    CommandRegistry registry;

    CommandDescriptor sync_cmd =
        sync_descriptor("link.get", Permission::read, recorder.handler());
    registry.add(std::move(sync_cmd));

    CommandDescriptor job_cmd;
    job_cmd.name = "link.export";
    job_cmd.summary = "a summary";
    job_cmd.kind = ExecutionKind::job;
    job_cmd.job_factory = noop_job_factory();
    registry.add(std::move(job_cmd));

    CallerContext caller;

    // dry_run on a command that does not support it -> unsupported.
    CommandRequest dry = request_for("link.get");
    dry.options.dry_run = true;
    const CommandResponse dry_resp = registry.execute(caller, dry);
    BOOST_TEST((dry_resp.error.code == CommandErrorCode::unsupported));
    BOOST_TEST(dry_resp.error.message == "dry_run not supported");

    // async on a non-async-capable sync command -> unsupported.
    CommandRequest async = request_for("link.get");
    async.options.async = true;
    const CommandResponse async_resp = registry.execute(caller, async);
    BOOST_TEST((async_resp.error.code == CommandErrorCode::unsupported));
    BOOST_TEST(async_resp.error.message == "async not supported");

    // A job descriptor with no submitter installed -> unsupported (fail
    // closed); the factory is never reached.
    const CommandResponse job_resp =
        registry.execute(caller, request_for("link.export"));
    BOOST_TEST((job_resp.error.code == CommandErrorCode::unsupported));

    // Neither dry_run nor async entered the sync handler.
    BOOST_TEST(recorder.calls.load() == 0);
}

// ---------------------------------------------------------------------------
// Defense-in-depth envelope sanity: a bad version or empty command yields
// invalid_envelope.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(execute_rejects_bad_envelope)
{
    CommandRegistry registry;
    registry.add(sync_descriptor("link.get",
                                 Permission::read,
                                 [](CommandContext&, const ArgsView&)
                                 {
                                     CommandResult r;
                                     r.result_json = "{}";
                                     return r;
                                 }));

    CallerContext caller;

    CommandRequest bad_version = request_for("link.get");
    bad_version.v = 2;
    const CommandResponse version_resp = registry.execute(caller, bad_version);
    BOOST_TEST((version_resp.error.code == CommandErrorCode::invalid_envelope));

    CommandRequest empty_command = request_for("");
    const CommandResponse empty_resp = registry.execute(caller, empty_command);
    BOOST_TEST((empty_resp.error.code == CommandErrorCode::invalid_envelope));
}

// ---------------------------------------------------------------------------
// execute_json: malformed JSON -> invalid_envelope; a valid envelope text
// round-trips through serialize_response.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(execute_json_parse_and_roundtrip)
{
    FakeHandlerRecorder recorder;
    CommandRegistry registry;
    registry.add(
        sync_descriptor("link.get", Permission::read, recorder.handler()));

    CallerContext caller;

    const CommandResponse malformed =
        registry.execute_json(caller, "{ not json");
    BOOST_TEST(malformed.ok == false);
    BOOST_TEST((malformed.error.code == CommandErrorCode::invalid_envelope));

    const CommandResponse good = registry.execute_json(
        caller, R"({"v":1,"id":"abc","command":"link.get"})");
    BOOST_TEST(good.ok == true);
    BOOST_TEST(good.id == "abc");
    BOOST_TEST(good.result_json == R"({"ok":true})");

    const std::string wire = serialize_response(good);
    BOOST_TEST(wire.find("\"abc\"") != std::string::npos);
    BOOST_TEST(wire.find(R"({"ok":true})") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Read-only concurrency smoke: 8 threads x 1000 calls on one registry all
// succeed with the echoed id. Must stay well under 2 s.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(concurrent_execute_is_consistent)
{
    FakeHandlerRecorder recorder;
    CommandRegistry registry;
    registry.add(
        sync_descriptor("link.get", Permission::read, recorder.handler()));

    constexpr int thread_count = 8;
    constexpr int per_thread = 1000;
    std::atomic<int> ok_count {0};

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back(
            [&registry, &ok_count]()
            {
                CallerContext caller;
                for (int i = 0; i < per_thread; ++i) {
                    const CommandResponse response =
                        registry.execute(caller, request_for("link.get"));
                    if (response.ok && response.id == "client-1"
                        && response.result_json == R"({"ok":true})")
                    {
                        ok_count.fetch_add(1);
                    }
                }
            });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    BOOST_TEST(ok_count.load() == thread_count * per_thread);
    BOOST_TEST(recorder.calls.load() == thread_count * per_thread);
}
