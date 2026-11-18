#define BOOST_TEST_MODULE TuiInProcessClientTest

#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/security/control_set.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/in_process_client.hpp>

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
using url_shortener::commands::ExecutionKind;
using url_shortener::commands::IJobContext;
using url_shortener::commands::JobBody;
using url_shortener::commands::JobInfo;
using url_shortener::commands::JobOutcome;
using url_shortener::commands::JobState;
using url_shortener::commands::make_error;
using url_shortener::commands::make_failure;
using url_shortener::commands::make_job_accepted;
using url_shortener::commands::make_success;
using url_shortener::commands::Permission;
using url_shortener::commands::serialize_response;
using url_shortener::tui::ArgInfo;
using url_shortener::tui::ArgKind;
using url_shortener::tui::CommandInfo;
using url_shortener::tui::InProcessClient;
using url_shortener::tui::reconcile_envelope;

namespace
{

// Builds a small registry covering every mapping the catalog must report:
// a read echo command, a throwing command, a write command, a job command,
// and one command exercising every ArgType / nullable / object-fields case.
void populate(CommandRegistry& registry)
{
    CommandDescriptor echo;
    echo.name = "echo.run";
    echo.summary = "Echo a fixed payload back";
    echo.permission = Permission::read;
    echo.handler = [](CommandContext&, const ArgsView&)
    {
        CommandResult result;
        result.result_json = R"({"echo":"ok"})";
        return result;
    };
    registry.add(std::move(echo));

    CommandDescriptor boom;
    boom.name = "boom.run";
    boom.summary = "A handler that throws";
    boom.permission = Permission::read;
    boom.handler = [](CommandContext&, const ArgsView&) -> CommandResult
    { throw std::runtime_error("kaboom"); };
    registry.add(std::move(boom));

    CommandDescriptor writer;
    writer.name = "thing.write";
    writer.summary = "A mutating write command";
    writer.permission = Permission::write;
    writer.mutating = true;
    writer.handler = [](CommandContext&, const ArgsView&)
    {
        CommandResult result;
        result.result_json = R"({"written":true})";
        return result;
    };
    registry.add(std::move(writer));

    CommandDescriptor exporter;
    exporter.name = "thing.export";
    exporter.summary = "An export job";
    exporter.permission = Permission::read;
    exporter.kind = ExecutionKind::job;
    exporter.job_factory =
        [](const CommandContext&,
           const ArgsView&) -> std::variant<JobBody, CommandError>
    {
        return JobBody(
            [](IJobContext&)
            {
                JobOutcome outcome;
                outcome.ok = true;
                outcome.result_json = std::string("{}");
                return outcome;
            });
    };
    registry.add(std::move(exporter));

    CommandDescriptor demo;
    demo.name = "schema.demo";
    demo.summary = "One argument of every kind";
    demo.permission = Permission::read;

    ArgSpec title;
    title.name = "title";
    title.type = ArgType::string;
    title.required = true;
    title.doc = "A title";
    title.default_value = std::string("\"hi\"");

    ArgSpec count;
    count.name = "count";
    count.type = ArgType::integer;
    count.min = 0;
    count.max = 10;

    ArgSpec enabled;
    enabled.name = "enabled";
    enabled.type = ArgType::boolean;

    ArgSpec mode;
    mode.name = "mode";
    mode.type = ArgType::enumeration;
    mode.enum_values = {"fast", "slow"};

    ArgSpec tags;
    tags.name = "tags";
    tags.type = ArgType::string_list;

    ArgSpec meta;
    meta.name = "meta";
    meta.type = ArgType::string_map;

    ArgSpec when;
    when.name = "when";
    when.type = ArgType::timestamp;
    when.nullable = true;

    ArgSpec campaign;
    campaign.name = "campaign";
    campaign.type = ArgType::object;
    ArgSpec field_name;
    field_name.name = "name";
    field_name.type = ArgType::string;
    ArgSpec field_source;
    field_source.name = "source";
    field_source.type = ArgType::string;
    campaign.fields = {field_name, field_source};

    demo.args = {title, count, enabled, mode, tags, meta, when, campaign};
    demo.handler = [](CommandContext&, const ArgsView&)
    {
        CommandResult result;
        result.result_json = R"({"ok":true})";
        return result;
    };
    registry.add(std::move(demo));
}

CommandRequest make_request(std::string command)
{
    CommandRequest request;
    request.v = 1;
    request.id = "req-1";
    request.command = std::move(command);
    request.args_json = "{}";
    return request;
}

const ArgInfo* find_arg(const CommandInfo& command, const std::string& name)
{
    for (const ArgInfo& arg : command.args) {
        if (arg.name == name) {
            return &arg;
        }
    }
    return nullptr;
}

const CommandInfo* find_command(const std::vector<CommandInfo>& commands,
                                const std::string& name)
{
    for (const CommandInfo& command : commands) {
        if (command.name == name) {
            return &command;
        }
    }
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// D12 / success criterion 5: the client returns the same bytes as the registry.
// On failure first check: the handler/echo descriptor or execute() changed.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(call_returns_registry_envelope_unchanged)
{
    CommandRegistry registry;
    populate(registry);
    CallerContext caller;
    caller.control_set = ControlSet::Admin;
    caller.principal = "local-operator";
    caller.transport = "tui";
    InProcessClient client(registry, caller);

    const CommandRequest request = make_request("echo.run");
    const CommandResponse via_client = client.call(request);
    const CommandResponse direct = registry.execute(caller, request);

    BOOST_TEST(via_client.ok);
    BOOST_TEST(via_client.result_json == R"({"echo":"ok"})");
    BOOST_TEST(serialize_response(via_client) == serialize_response(direct));
}

// ---------------------------------------------------------------------------
// An unknown command's envelope passes straight through.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(unknown_command_passes_through)
{
    CommandRegistry registry;
    populate(registry);
    CallerContext caller;
    caller.control_set = ControlSet::Admin;
    InProcessClient client(registry, caller);

    const CommandResponse response = client.call(make_request("no.such"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::unknown_command));
}

// ---------------------------------------------------------------------------
// A throwing handler becomes an internal error; no exception escapes call().
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(handler_throwing_yields_internal_no_exception)
{
    CommandRegistry registry;
    populate(registry);
    CallerContext caller;
    caller.control_set = ControlSet::Admin;
    InProcessClient client(registry, caller);

    CommandResponse response;
    BOOST_REQUIRE_NO_THROW(response = client.call(make_request("boom.run")));
    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::internal));
}

// ---------------------------------------------------------------------------
// commands() reports every descriptor with the right kind/flags/arg mapping.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(commands_catalog_maps_every_descriptor)
{
    CommandRegistry registry;
    populate(registry);
    CallerContext caller;
    caller.control_set = ControlSet::Admin;
    InProcessClient client(registry, caller);

    const std::vector<CommandInfo> catalog = client.commands();
    BOOST_TEST(catalog.size() == 5u);

    const CommandInfo* writer = find_command(catalog, "thing.write");
    BOOST_REQUIRE(writer != nullptr);
    BOOST_TEST(writer->mutating);
    BOOST_TEST(!writer->is_job);
    BOOST_TEST(writer->permission == "write");

    const CommandInfo* exporter = find_command(catalog, "thing.export");
    BOOST_REQUIRE(exporter != nullptr);
    BOOST_TEST(exporter->is_job);
    BOOST_TEST(!exporter->mutating);

    const CommandInfo* demo = find_command(catalog, "schema.demo");
    BOOST_REQUIRE(demo != nullptr);
    BOOST_TEST(demo->permission == "read");

    const ArgInfo* title = find_arg(*demo, "title");
    BOOST_REQUIRE(title != nullptr);
    BOOST_TEST((title->kind == ArgKind::string));
    BOOST_TEST(title->required);
    BOOST_TEST(title->doc == "A title");
    BOOST_REQUIRE(title->default_value.has_value());
    BOOST_TEST(*title->default_value == "\"hi\"");

    BOOST_TEST((find_arg(*demo, "count")->kind == ArgKind::integer));
    BOOST_TEST((find_arg(*demo, "enabled")->kind == ArgKind::boolean));
    BOOST_TEST((find_arg(*demo, "tags")->kind == ArgKind::string_list));
    BOOST_TEST((find_arg(*demo, "meta")->kind == ArgKind::string_map));

    const ArgInfo* mode = find_arg(*demo, "mode");
    BOOST_REQUIRE(mode != nullptr);
    BOOST_TEST((mode->kind == ArgKind::enumeration));
    BOOST_REQUIRE(mode->enum_values.size() == 2u);
    BOOST_TEST(mode->enum_values[0] == "fast");
    BOOST_TEST(mode->enum_values[1] == "slow");

    const ArgInfo* when = find_arg(*demo, "when");
    BOOST_REQUIRE(when != nullptr);
    BOOST_TEST((when->kind == ArgKind::timestamp));
    BOOST_TEST(when->nullable);

    const ArgInfo* campaign = find_arg(*demo, "campaign");
    BOOST_REQUIRE(campaign != nullptr);
    BOOST_TEST((campaign->kind == ArgKind::object));
    BOOST_REQUIRE(campaign->members.size() == 2u);
    BOOST_TEST(campaign->members[0] == "name");
    BOOST_TEST(campaign->members[1] == "source");
}

// ---------------------------------------------------------------------------
// session().read_only follows the caller's control set.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(session_read_only_tracks_control_set)
{
    CommandRegistry registry;
    populate(registry);

    CallerContext user;
    user.control_set = ControlSet::User;
    user.principal = "viewer";
    user.transport = "tui";
    InProcessClient user_client(registry, user);
    BOOST_TEST(user_client.session().read_only);
    BOOST_TEST(user_client.session().principal == "viewer");
    BOOST_TEST(user_client.session().transport == "tui");

    CallerContext admin;
    admin.control_set = ControlSet::Admin;
    InProcessClient admin_client(registry, admin);
    BOOST_TEST(!admin_client.session().read_only);
}

// ---------------------------------------------------------------------------
// A read-only caller invoking a write command is denied, not an exception.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(read_only_caller_on_write_is_not_authorized)
{
    CommandRegistry registry;
    populate(registry);
    CallerContext user;
    user.control_set = ControlSet::User;
    InProcessClient client(registry, user);

    CommandResponse response;
    BOOST_REQUIRE_NO_THROW(response = client.call(make_request("thing.write")));
    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::not_authorized));
}

// ---------------------------------------------------------------------------
// T-113: a self-contradictory envelope is converted to an internal error,
// never surfaced as a false success. Consistent envelopes pass unchanged.
// On failure first check: reconcile_envelope's agreement rules.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(reconcile_rejects_contradictory_envelopes_t113)
{
    // ok:true carrying an error.
    CommandResponse ok_with_error;
    ok_with_error.id = "z1";
    ok_with_error.ok = true;
    ok_with_error.error = make_error(CommandErrorCode::slug_conflict, "x");
    const CommandResponse fixed_a = reconcile_envelope(ok_with_error);
    BOOST_TEST(!fixed_a.ok);
    BOOST_TEST((fixed_a.error.code == CommandErrorCode::internal));
    BOOST_TEST(fixed_a.id == "z1");

    // ok:false carrying a result.
    CommandResponse fail_with_result;
    fail_with_result.id = "z2";
    fail_with_result.ok = false;
    fail_with_result.result_json = "{}";
    const CommandResponse fixed_b = reconcile_envelope(fail_with_result);
    BOOST_TEST(!fixed_b.ok);
    BOOST_TEST((fixed_b.error.code == CommandErrorCode::internal));

    // ok:true with neither result nor job.
    CommandResponse ok_with_nothing;
    ok_with_nothing.id = "z3";
    ok_with_nothing.ok = true;
    const CommandResponse fixed_c = reconcile_envelope(ok_with_nothing);
    BOOST_TEST(!fixed_c.ok);
    BOOST_TEST((fixed_c.error.code == CommandErrorCode::internal));

    // ok:false with neither result nor error.
    CommandResponse fail_with_nothing;
    fail_with_nothing.id = "z4";
    fail_with_nothing.ok = false;
    const CommandResponse fixed_d = reconcile_envelope(fail_with_nothing);
    BOOST_TEST(!fixed_d.ok);
    BOOST_TEST((fixed_d.error.code == CommandErrorCode::internal));
}

BOOST_AUTO_TEST_CASE(reconcile_passes_consistent_envelopes_t113)
{
    const CommandResponse success =
        reconcile_envelope(make_success("ok1", R"({"a":1})"));
    BOOST_TEST(success.ok);
    BOOST_TEST(success.result_json == R"({"a":1})");

    const CommandResponse failure = reconcile_envelope(
        make_failure("ok2", make_error(CommandErrorCode::not_found, "nope")));
    BOOST_TEST(!failure.ok);
    BOOST_TEST((failure.error.code == CommandErrorCode::not_found));

    JobInfo job;
    job.id = "j-1";
    job.state = JobState::queued;
    const CommandResponse accepted =
        reconcile_envelope(make_job_accepted("ok3", job));
    BOOST_TEST(accepted.ok);
    BOOST_REQUIRE(accepted.job.has_value());
    BOOST_TEST(accepted.job->id == "j-1");
}
