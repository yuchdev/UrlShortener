#define BOOST_TEST_MODULE CommandsErrorCodeCoverageTest
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/app/app_error.hpp"
#include "url_shortener/commands/arg_spec.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/job_submitter.hpp"

using url_shortener::commands::all_error_infos;
using url_shortener::commands::ArgSpec;
using url_shortener::commands::ArgsView;
using url_shortener::commands::ArgType;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandContext;
using url_shortener::commands::CommandDescriptor;
using url_shortener::commands::CommandError;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::CommandResult;
using url_shortener::commands::error_info;
using url_shortener::commands::error_info_for;
using url_shortener::commands::ExecutionKind;
using url_shortener::commands::exit_code_for;
using url_shortener::commands::from_app_error;
using url_shortener::commands::http_status_for;
using url_shortener::commands::IJobContext;
using url_shortener::commands::IJobSubmitter;
using url_shortener::commands::JobBody;
using url_shortener::commands::JobOutcome;
using url_shortener::commands::JobSubmitResult;
using url_shortener::commands::make_error;
using url_shortener::commands::parse_response;
using url_shortener::commands::Permission;
using url_shortener::commands::serialize_response;

namespace
{

/// One end-to-end way to produce a single @ref CommandErrorCode. `run` builds
/// whatever descriptors/submitter the code needs into the supplied (fresh)
/// registry, executes a request, and returns the resulting response. Driving
/// every code through `CommandRegistry` (not by hand-constructing a response)
/// is what proves the code survives the real pipeline, serialization, status
/// and exit-code lookup (subtask 13 objective 1).
struct code_scenario
{
    ///< The error code this scenario is expected to produce.
    CommandErrorCode code;

    ///< Drives a fresh registry and returns the produced response.
    std::function<CommandResponse(CommandRegistry&)> run;
};

/// A submitter that always rejects with `busy`, so a job request surfaces the
/// `busy` code through the real submit path.
struct BusySubmitter : public IJobSubmitter
{
    JobSubmitResult submit(std::string, std::string, JobBody) override
    {
        JobSubmitResult result;
        result.error = make_error(CommandErrorCode::busy, "queue full");
        return result;
    }
};

/// Builds a caller with the given role; the string fields are diagnostic only.
CallerContext make_caller(ControlSet role)
{
    CallerContext caller;
    caller.principal = "tester";
    caller.control_set = role;
    caller.transport = "test";
    return caller;
}

/// Builds a v1 request for @p command carrying @p args_json verbatim.
CommandRequest make_request(std::string command, std::string args_json = "{}")
{
    CommandRequest request;
    request.v = 1;
    request.id = "client-1";
    request.command = std::move(command);
    request.args_json = std::move(args_json);
    return request;
}

/// A read descriptor whose sync handler returns a fixed expected failure.
CommandDescriptor error_descriptor(std::string name, CommandErrorCode code)
{
    CommandDescriptor descriptor;
    descriptor.name = std::move(name);
    descriptor.summary = "returns a fixed error";
    descriptor.permission = Permission::read;
    descriptor.handler = [code](CommandContext&, const ArgsView&)
    {
        CommandResult result;
        result.error = make_error(code, "fixed reason");
        return result;
    };
    return descriptor;
}

/// A read descriptor whose handler maps @p app_code through `from_app_error`,
/// exercising the application-to-command error mapping end to end.
CommandDescriptor app_error_descriptor(
    std::string name, url_shortener::app::AppErrorCode app_code)
{
    CommandDescriptor descriptor;
    descriptor.name = std::move(name);
    descriptor.summary = "returns a mapped app error";
    descriptor.permission = Permission::read;
    descriptor.handler = [app_code](CommandContext&, const ArgsView&)
    {
        CommandResult result;
        result.error = make_error(from_app_error(app_code), "fixed reason");
        return result;
    };
    return descriptor;
}

/// A read descriptor whose sync handler succeeds with a trivial result.
CommandDescriptor ok_descriptor(std::string name)
{
    CommandDescriptor descriptor;
    descriptor.name = std::move(name);
    descriptor.summary = "a trivial success";
    descriptor.permission = Permission::read;
    descriptor.handler = [](CommandContext&, const ArgsView&)
    {
        CommandResult result;
        result.result_json = "{}";
        return result;
    };
    return descriptor;
}

/// The full scenario table: exactly one scenario per non-`none`
/// @ref CommandErrorCode. Adding an enumerator without adding a row here makes
/// `every_enumerator_has_a_scenario` fail (subtask 13 success criterion 2).
const std::vector<code_scenario>& all_scenarios()
{
    static const std::vector<code_scenario> scenarios = {
        // Malformed envelope text rejected by the parser inside execute_json.
        {CommandErrorCode::invalid_envelope,
         [](CommandRegistry& registry)
         {
             return registry.execute_json(make_caller(ControlSet::Admin),
                                          "not json");
         }},

        // A command name that resolves to no descriptor.
        {CommandErrorCode::unknown_command,
         [](CommandRegistry& registry)
         {
             return registry.execute(make_caller(ControlSet::Admin),
                                     make_request("no.such"));
         }},

        // A required argument missing from the args object.
        {CommandErrorCode::invalid_field,
         [](CommandRegistry& registry)
         {
             CommandDescriptor descriptor;
             descriptor.name = "needs.arg";
             descriptor.summary = "requires a slug";
             descriptor.permission = Permission::read;
             ArgSpec slug;
             slug.name = "slug";
             slug.type = ArgType::string;
             slug.required = true;
             descriptor.args.push_back(slug);
             descriptor.handler = [](CommandContext&, const ArgsView&)
             {
                 CommandResult result;
                 result.result_json = "{}";
                 return result;
             };
             registry.add(std::move(descriptor));
             return registry.execute(make_caller(ControlSet::Admin),
                                     make_request("needs.arg", "{}"));
         }},

        // A write command refused for a read-only caller.
        {CommandErrorCode::invalid_url,
         [](CommandRegistry& registry)
         {
             registry.add(app_error_descriptor(
                 "app.err", url_shortener::app::AppErrorCode::invalid_url));
             return registry.execute(make_caller(ControlSet::Admin),
                                     make_request("app.err"));
         }},

        {CommandErrorCode::invalid_slug,
         [](CommandRegistry& registry)
         {
             registry.add(app_error_descriptor(
                 "app.err", url_shortener::app::AppErrorCode::invalid_slug));
             return registry.execute(make_caller(ControlSet::Admin),
                                     make_request("app.err"));
         }},

        {CommandErrorCode::reserved_slug,
         [](CommandRegistry& registry)
         {
             registry.add(app_error_descriptor(
                 "app.err", url_shortener::app::AppErrorCode::reserved_slug));
             return registry.execute(make_caller(ControlSet::Admin),
                                     make_request("app.err"));
         }},

        {CommandErrorCode::slug_conflict,
         [](CommandRegistry& registry)
         {
             registry.add(app_error_descriptor(
                 "app.err", url_shortener::app::AppErrorCode::slug_conflict));
             return registry.execute(make_caller(ControlSet::Admin),
                                     make_request("app.err"));
         }},

        {CommandErrorCode::not_found,
         [](CommandRegistry& registry)
         {
             registry.add(app_error_descriptor(
                 "app.err", url_shortener::app::AppErrorCode::not_found));
             return registry.execute(make_caller(ControlSet::Admin),
                                     make_request("app.err"));
         }},

        {CommandErrorCode::not_authorized,
         [](CommandRegistry& registry)
         {
             CommandDescriptor descriptor;
             descriptor.name = "do.write";
             descriptor.summary = "a write command";
             descriptor.permission = Permission::write;
             descriptor.mutating = true;
             descriptor.handler = [](CommandContext&, const ArgsView&)
             {
                 CommandResult result;
                 result.result_json = "{}";
                 return result;
             };
             registry.add(std::move(descriptor));
             return registry.execute(make_caller(ControlSet::User),
                                     make_request("do.write"));
         }},

        // dry_run requested on a command that does not support it.
        {CommandErrorCode::unsupported,
         [](CommandRegistry& registry)
         {
             registry.add(ok_descriptor("no.dryrun"));
             CommandRequest request = make_request("no.dryrun");
             request.options.dry_run = true;
             return registry.execute(make_caller(ControlSet::Admin), request);
         }},

        // A job accepted by the factory but rejected by a busy submitter.
        {CommandErrorCode::busy,
         [](CommandRegistry& registry)
         {
             CommandDescriptor descriptor;
             descriptor.name = "job.run";
             descriptor.summary = "a job command";
             descriptor.permission = Permission::read;
             descriptor.kind = ExecutionKind::job;
             descriptor.job_factory =
                 [](const CommandContext&,
                    const ArgsView&) -> std::variant<JobBody, CommandError>
             { return JobBody {[](IJobContext&) { return JobOutcome {}; }}; };
             registry.add(std::move(descriptor));
             registry.set_job_submitter(std::make_shared<BusySubmitter>());
             return registry.execute(make_caller(ControlSet::Admin),
                                     make_request("job.run"));
         }},

        // A handler that reports a cancelled job through its result.
        {CommandErrorCode::job_cancelled,
         [](CommandRegistry& registry)
         {
             registry.add(
                 error_descriptor("job.get", CommandErrorCode::job_cancelled));
             return registry.execute(make_caller(ControlSet::Admin),
                                     make_request("job.get"));
         }},

        {CommandErrorCode::storage_failure,
         [](CommandRegistry& registry)
         {
             registry.add(app_error_descriptor(
                 "app.err", url_shortener::app::AppErrorCode::storage_failure));
             return registry.execute(make_caller(ControlSet::Admin),
                                     make_request("app.err"));
         }},

        // A handler that throws; its what() must never surface (T-10).
        {CommandErrorCode::internal,
         [](CommandRegistry& registry)
         {
             CommandDescriptor descriptor;
             descriptor.name = "will.throw";
             descriptor.summary = "throws on dispatch";
             descriptor.permission = Permission::read;
             descriptor.handler = [](CommandContext&,
                                     const ArgsView&) -> CommandResult
             { throw std::runtime_error("secret-dsn-value"); };
             registry.add(std::move(descriptor));
             return registry.execute(make_caller(ControlSet::Admin),
                                     make_request("will.throw"));
         }},
    };
    return scenarios;
}

}  // namespace

// Type: unit (cross-cutting error-code coverage guard).
// Scenario: every CommandErrorCode other than `none` has a scenario that drives
//   it end to end through CommandRegistry.
// Boundaries: adding an enumerator to CommandErrorCode (or deleting a scenario)
//   makes this fail because a code would have no scenario.
// On failure first check: did a new error code land in error_codes.hpp without
//   a matching row added to all_scenarios() in this file?

BOOST_AUTO_TEST_CASE(every_enumerator_has_a_scenario)
{
    std::set<CommandErrorCode> covered;
    for (const code_scenario& scenario : all_scenarios()) {
        covered.insert(scenario.code);
    }

    // One scenario per distinct code: no accidental duplicate row.
    BOOST_TEST(covered.size() == all_scenarios().size());

    std::size_t expected = 0;
    for (const error_info& info : all_error_infos()) {
        if (info.code == CommandErrorCode::none) {
            continue;
        }
        ++expected;
        BOOST_TEST(covered.count(info.code) == 1u,
                   "no scenario for code: " << info.name);
    }
    BOOST_TEST(covered.size() == expected);
}

// Type: unit (error-code round-trip guard).
// Scenario: each scenario produces exactly its code, and that code survives
//   serialize_response -> parse_response, http_status_for and exit_code_for,
//   all equal to the one subtask-02 table row.
// Boundaries: an error response always has ok == false; job_cancelled keeps
//   HTTP 200 / exit 4 while being a failure.
// On failure first check: did a scenario stop reaching its descriptor (e.g. a
//   permission or validation change), or did the error table row for the code
//   change in error_codes.cpp?

BOOST_AUTO_TEST_CASE(each_code_round_trips_through_the_pipeline)
{
    for (const code_scenario& scenario : all_scenarios()) {
        const error_info& row = error_info_for(scenario.code);
        BOOST_TEST_CONTEXT("code=" << row.name)
        {
            CommandRegistry registry;
            const CommandResponse response = scenario.run(registry);

            // Every scenario here produces a failure envelope.
            BOOST_TEST(response.ok == false);
            BOOST_TEST((response.error.code == scenario.code));

            // Status and exit code come only from the one table.
            BOOST_TEST(http_status_for(response) == row.http_status);
            BOOST_TEST(exit_code_for(response) == row.exit_code);

            // Serialize and parse back; the code survives the wire form.
            const std::string wire = serialize_response(response);
            const auto parsed = parse_response(wire);
            BOOST_REQUIRE_MESSAGE(parsed.response.has_value(),
                                  "parse_response failed: " << parsed.error);
            BOOST_TEST((parsed.response->error.code == scenario.code));
            BOOST_TEST(http_status_for(*parsed.response) == row.http_status);
            BOOST_TEST(exit_code_for(*parsed.response) == row.exit_code);
        }
    }
}

// Type: unit (information-disclosure regression, threat model T-10).
// Scenario: a handler that throws std::runtime_error("secret-dsn-value") yields
//   an `internal` response whose wire form carries the fixed message only.
// Boundaries: the exception what() text must appear nowhere in the output.
// On failure first check: did execute stop wrapping the handler dispatch in a
//   catch that maps to a fixed "internal error" message?

BOOST_AUTO_TEST_CASE(internal_never_leaks_exception_text)
{
    CommandRegistry registry;
    CommandResponse response;
    for (const code_scenario& scenario : all_scenarios()) {
        if (scenario.code == CommandErrorCode::internal) {
            response = scenario.run(registry);
            break;
        }
    }

    BOOST_TEST((response.error.code == CommandErrorCode::internal));
    BOOST_TEST(response.error.message == "internal error");

    const std::string wire = serialize_response(response);
    BOOST_TEST(wire.find("secret-dsn-value") == std::string::npos);
}
