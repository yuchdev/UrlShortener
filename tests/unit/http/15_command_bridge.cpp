#define BOOST_TEST_MODULE CommandBridge
#include <boost/test/unit_test.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include <boost/beast/http.hpp>

#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/http/command_bridge.hpp>
#include <url_shortener/security/control_set.hpp>

#ifdef _WIN32
#    include <process.h>
#else
#    include <unistd.h>
#endif

namespace bhttp = boost::beast::http;

namespace commands = url_shortener::commands;
namespace composition = url_shortener::composition;
using url_shortener::http::BeastRequest;
using url_shortener::http::BeastResponse;
using url_shortener::http::execute_resource_command;
using url_shortener::http::legacy_error_code;
using url_shortener::http::request_runtime;
using url_shortener::http::resource_response;
using url_shortener::http::resource_route_caller;
using url_shortener::http::rest_policy_refusal;

namespace
{

/// Request id the tests inject via the internal correlation header and expect
/// echoed verbatim in every error envelope body.
constexpr const char* k_request_id = "req-bridge-42";

BeastRequest make_request()
{
    BeastRequest req {bhttp::verb::get, "/api/v1/links/x", 11};
    req.set(bhttp::field::host, "short.test");
    // Sessions write the resolved id to the internal header before dispatch;
    // the response helpers read it back for the body's request_id.
    req.set("X-Internal-Request-Id", k_request_id);
    return req;
}

ServerConfig test_config()
{
    ServerConfig config;
    config.shortener_allow_private_targets = true;
    config.shortener_generated_slug_length = 8;
    return config;
}

bool body_has(const BeastResponse& res, const std::string& needle)
{
    return res.body().find(needle) != std::string::npos;
}

/// A unique temp path (pid + per-call counter) so overlapping ctest runs of
/// this executable never collide on the SQLite file or its YAML.
std::filesystem::path unique_temp_path(const std::string& suffix)
{
    static int counter = 0;
#ifdef _WIN32
    const long pid = static_cast<long>(_getpid());
#else
    const long pid = static_cast<long>(::getpid());
#endif
    return std::filesystem::temp_directory_path()
        / ("urlsh_bridge15_" + std::to_string(pid) + "_"
           + std::to_string(++counter) + suffix);
}

/// Writes @p content to @p path, truncating any existing file.
void write_file(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

/// Records what a stub command handler observed, so a test can assert the
/// command name, args text, request id and caller passed straight through.
struct recorded
{
    bool called = false;
    std::string principal;
    std::string transport;
    ControlSet control_set = ControlSet::User;
    std::string command;
    std::string args_json;
    std::string id;
};

/// Builds a sync descriptor with the given name/permission whose handler marks
/// @p rec called and copies the context it saw.
commands::CommandDescriptor make_descriptor(const std::string& name,
                                            const commands::Permission permission,
                                            std::shared_ptr<recorded> rec,
                                            const bool with_note_arg = false)
{
    commands::CommandDescriptor descriptor;
    descriptor.name = name;
    descriptor.summary = "stub " + name;
    descriptor.permission = permission;
    descriptor.kind = commands::ExecutionKind::sync;
    if (with_note_arg) {
        commands::ArgSpec note;
        note.name = "note";
        note.type = commands::ArgType::string;
        descriptor.args.push_back(note);
    }
    descriptor.handler = [rec](commands::CommandContext& ctx,
                               const commands::ArgsView&) {
        rec->called = true;
        rec->principal = ctx.caller.principal;
        rec->transport = ctx.caller.transport;
        rec->control_set = ctx.caller.control_set;
        rec->command = ctx.request.command;
        rec->args_json = ctx.request.args_json;
        rec->id = ctx.request.id;
        commands::CommandResult result;
        result.result_json = R"({"stub":true})";
        return result;
    };
    return descriptor;
}

}  // namespace

// Type: unit (error mapping parity).
// Scenario: resource_response renders every CommandErrorCode's failure with the
//   legacy REST status, the legacy error code, the error message, and the
//   request id taken from the HTTP request header.
// Boundaries: the status/code come only from the 01.0/02 legacy columns; the
//   message is value-free service text. On failure first check: did the legacy
//   columns in all_error_infos() drift, or did resource_response stop reading
//   the request-scoped id?
BOOST_AUTO_TEST_CASE(every_error_code_maps_to_legacy_status_and_code)
{
    const auto req = make_request();
    const auto config = test_config();

    for (const commands::error_info& info : commands::all_error_infos()) {
        commands::CommandError error {info.code, "boom"};
        const commands::CommandResponse response =
            commands::make_failure("ignored-envelope-id", error);
        const BeastResponse http =
            resource_response(req, config, false, response);

        BOOST_TEST(http.result_int() == info.legacy_rest_status);
        BOOST_TEST(body_has(http,
                            "\"code\":\"" + std::string(info.legacy_rest_code)
                                + "\""));
        BOOST_TEST(body_has(http, "\"message\":\"boom\""));
        BOOST_TEST(body_has(http,
                            "\"request_id\":\"" + std::string(k_request_id)
                                + "\""));
        BOOST_TEST(std::string(http[bhttp::field::content_type])
                   == "application/json");
    }
}

// Type: unit (stats detail promotion).
// Scenario: legacy_error_code and resource_response promote an invalid_field
//   message of exactly invalid_timestamp / invalid_bucket to that code, while
//   every other invalid_field message (slug: missing, range_too_large) and the
//   same token under a non-invalid_field code stay unpromoted.
// Boundaries: promotion is scoped to invalid_field and the two fixed tokens
//   (T-62); the amended range_too_large detail is NOT promoted (stays
//   invalid_request). On failure first check: did the promotion predicate widen
//   its token set or lose the invalid_field scope?
BOOST_AUTO_TEST_CASE(invalid_field_detail_promotion)
{
    const auto req = make_request();
    const auto config = test_config();

    const commands::CommandError ts {commands::CommandErrorCode::invalid_field,
                                     "invalid_timestamp"};
    BOOST_TEST(legacy_error_code(ts) == "invalid_timestamp");
    const BeastResponse ts_http =
        resource_response(req, config, false, commands::make_failure("i", ts));
    BOOST_TEST(ts_http.result_int() == 400);
    BOOST_TEST(body_has(ts_http, "\"code\":\"invalid_timestamp\""));

    const commands::CommandError bucket {
        commands::CommandErrorCode::invalid_field, "invalid_bucket"};
    BOOST_TEST(legacy_error_code(bucket) == "invalid_bucket");
    BOOST_TEST(body_has(
        resource_response(req, config, false, commands::make_failure("i", bucket)),
        "\"code\":\"invalid_bucket\""));

    const commands::CommandError missing {
        commands::CommandErrorCode::invalid_field, "slug: missing"};
    BOOST_TEST(legacy_error_code(missing) == "invalid_request");

    const commands::CommandError range {
        commands::CommandErrorCode::invalid_field, "range_too_large"};
    BOOST_TEST(legacy_error_code(range) == "invalid_request");

    // The same token under a different code is not promoted.
    const commands::CommandError not_scoped {
        commands::CommandErrorCode::internal, "invalid_timestamp"};
    BOOST_TEST(legacy_error_code(not_scoped) == "internal_error");
}

// Type: unit (success rendering).
// Scenario: a successful result is rendered with the caller-chosen status (200
//   and 201), the result_json embedded byte-for-byte, and application/json.
// Boundaries: the success body is never re-encoded. On failure first check: did
//   resource_response start wrapping or re-serializing result_json?
BOOST_AUTO_TEST_CASE(success_renders_result_json_verbatim)
{
    const auto req = make_request();
    const auto config = test_config();
    const std::string result = R"({"slug":"abc","target_url":"https://e.test/"})";

    const BeastResponse ok200 = resource_response(
        req, config, false, commands::make_success("i", result), 200);
    BOOST_TEST(ok200.result_int() == 200);
    BOOST_TEST(ok200.body() == result);
    BOOST_TEST(std::string(ok200[bhttp::field::content_type])
               == "application/json");

    const BeastResponse ok201 = resource_response(
        req, config, false, commands::make_success("i", result), 201);
    BOOST_TEST(ok201.result_int() == 201);
    BOOST_TEST(ok201.body() == result);
}

// Type: unit (job response rejection, T-63).
// Scenario: a job-carrying response, which no resource route produces, maps to
//   500 internal_error rather than leaking a job envelope.
// Boundaries: resource routes have no job-rendering path. On failure first
//   check: did resource_response gain a job branch it must not have?
BOOST_AUTO_TEST_CASE(job_response_maps_to_internal_error)
{
    const auto req = make_request();
    const auto config = test_config();

    commands::JobInfo job;
    job.id = "j-1";
    job.state = commands::JobState::queued;
    const BeastResponse http = resource_response(
        req, config, false, commands::make_job_accepted("i", job));

    BOOST_TEST(http.result_int() == 500);
    BOOST_TEST(body_has(http, "\"code\":\"internal_error\""));
}

// Type: unit (caller + passthrough against a stub registry).
// Scenario: execute_resource_command against a stub registry forwards the
//   command name, args text and request id unchanged, and the explicit caller
//   (the legacy Admin/rest context) reaches the handler.
// Boundaries: the bridge adds only transport policy, never args/caller mutation
//   (C7). On failure first check: did the CommandRequest assembly reorder or
//   drop a field?
BOOST_AUTO_TEST_CASE(execute_passes_command_args_id_and_caller_through)
{
    auto rec = std::make_shared<recorded>();
    commands::CommandRegistry registry;
    registry.add(make_descriptor("test.echo",
                                 commands::Permission::read,
                                 rec,
                                 /*with_note_arg=*/true));

    const commands::CommandResponse response = execute_resource_command(
        registry, "test.echo", R"({"note":"hi"})", "rid-9", resource_route_caller());

    BOOST_TEST(response.ok);
    BOOST_TEST(response.id == "rid-9");
    BOOST_TEST(rec->called);
    BOOST_TEST(rec->command == "test.echo");
    BOOST_TEST(rec->args_json == R"({"note":"hi"})");
    BOOST_TEST(rec->id == "rid-9");
    BOOST_TEST(rec->principal == "rest-resource");
    BOOST_TEST(rec->transport == "rest");
    BOOST_TEST((rec->control_set == ControlSet::Admin));
}

// Type: unit (REST transport policy, T-61).
// Scenario: rest_policy_refusal refuses a manage_users descriptor for every
//   caller including Admin, returns a not_authorized envelope, and the handler
//   never runs; read/write descriptors and unknown commands yield nullopt.
// Boundaries: the refusal is a transport rule applied before execute (D4). On
//   failure first check: did the predicate stop keying on manage_users, or did
//   execute_resource_command stop honoring the refusal?
BOOST_AUTO_TEST_CASE(manage_users_is_refused_before_execute)
{
    auto manage_rec = std::make_shared<recorded>();
    auto read_rec = std::make_shared<recorded>();
    auto write_rec = std::make_shared<recorded>();
    commands::CommandRegistry registry;
    registry.add(
        make_descriptor("user.create", commands::Permission::manage_users, manage_rec));
    registry.add(make_descriptor("test.read", commands::Permission::read, read_rec));
    registry.add(make_descriptor("test.write", commands::Permission::write, write_rec));

    const auto refusal = rest_policy_refusal(registry, "user.create", "id1");
    BOOST_REQUIRE(refusal.has_value());
    BOOST_TEST(!refusal->ok);
    BOOST_TEST((refusal->error.code == commands::CommandErrorCode::not_authorized));
    BOOST_TEST(refusal->id == "id1");
    BOOST_TEST(!manage_rec->called);

    // read/write descriptors and unknown names are let through (nullopt).
    BOOST_TEST(!rest_policy_refusal(registry, "test.read", "id").has_value());
    BOOST_TEST(!rest_policy_refusal(registry, "test.write", "id").has_value());
    BOOST_TEST(!rest_policy_refusal(registry, "no.such", "id").has_value());

    // Even an Admin caller cannot reach a manage_users handler through the
    // bridge's execute path.
    commands::CallerContext admin;
    admin.principal = "p";
    admin.control_set = ControlSet::Admin;
    admin.transport = "rest";
    const commands::CommandResponse via_execute =
        execute_resource_command(registry, "user.create", "{}", "id2", admin);
    BOOST_TEST(!via_execute.ok);
    BOOST_TEST(
        (via_execute.error.code == commands::CommandErrorCode::not_authorized));
    BOOST_TEST(!manage_rec->called);
}

// Type: unit (real default runtime end to end).
// Scenario: link.get for a missing slug against the real per-request runtime
//   yields a 404 not_found body identical to the legacy linkNotFound shape
//   (message "Link not found").
// Boundaries: the bridge builds the one product registry per request (C3/C11)
//   over the process-wide legacy store. On failure first check: did the service
//   not_found detail change, or did the legacy status/code columns drift?
BOOST_AUTO_TEST_CASE(real_runtime_link_get_missing_slug_is_not_found)
{
    url_shortener::app::LinkBackend::reset();
    composition::reset_process_command_runtime();
    const auto req = make_request();
    const auto config = test_config();

    const commands::CommandResponse response = execute_resource_command(
        config, "link.get", R"({"slug":"zzmissing-bridge"})", "rid");
    const BeastResponse http = resource_response(req, config, false, response);

    BOOST_TEST(http.result_int() == 404);
    BOOST_TEST(body_has(http, "\"code\":\"not_found\""));
    BOOST_TEST(body_has(http, "\"message\":\"Link not found\""));
}

// Type: unit (explicit caller authorization, C7).
// Scenario: the explicit-caller overload forwards a User caller to execute,
//   which denies a write (link.create) with 403 not_authorized.
// Boundaries: authorization is performed only in execute; the bridge supplies
//   the caller (C7). On failure first check: did the caller stop flowing into
//   execute, or did link.create change its permission?
BOOST_AUTO_TEST_CASE(explicit_user_caller_is_denied_write_by_execute)
{
    url_shortener::app::LinkBackend::reset();
    composition::reset_process_command_runtime();
    const auto req = make_request();
    const auto config = test_config();

    commands::CallerContext user;
    user.principal = "someone";
    user.control_set = ControlSet::User;
    user.transport = "rest";

    const commands::CommandResponse response =
        execute_resource_command(config, "link.create", "{}", "rid", user);
    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == commands::CommandErrorCode::not_authorized));

    const BeastResponse http = resource_response(req, config, false, response);
    BOOST_TEST(http.result_int() == 403);
    BOOST_TEST(body_has(http, "\"code\":\"not_authorized\""));
}

// Type: unit (non-identifying audit labels, T-84).
// Scenario: the legacy resource-route caller carries fixed, non-identifying
//   principal and transport labels and never an IP, port or token.
// Boundaries: the labels feed the audit line and must disclose nothing about
//   the peer (T-84). On failure first check: did resource_route_caller start
//   folding request-derived identity into principal/transport?
BOOST_AUTO_TEST_CASE(resource_route_caller_principal_is_non_identifying)
{
    const commands::CallerContext caller = resource_route_caller();
    BOOST_TEST(caller.principal == "rest-resource");
    BOOST_TEST(caller.transport == "rest");
    BOOST_TEST((caller.control_set == ControlSet::Admin));

    // No peer IP/port/token could sit in these fixed labels.
    BOOST_TEST(caller.principal.find(':') == std::string::npos);
    BOOST_TEST(caller.principal.find_first_of("0123456789")
               == std::string::npos);
    BOOST_TEST(caller.transport.find_first_of("0123456789")
               == std::string::npos);
}

// Type: unit (shared default runtime construction).
// Scenario: request_runtime returns a non-null product registry wired over the
//   legacy process-wide holders when no process runtime is installed, usable
//   without any prior backend install.
// Boundaries: the fallback default is built over an inactive handle (C3). On
//   failure first check: did build_command_runtime stop populating the registry
//   or start requiring an active backend?
BOOST_AUTO_TEST_CASE(request_runtime_builds_a_populated_registry)
{
    url_shortener::app::LinkBackend::reset();
    composition::reset_process_command_runtime();
    const auto runtime = request_runtime();
    BOOST_REQUIRE(runtime != nullptr);
    BOOST_REQUIRE(runtime->registry != nullptr);
    BOOST_TEST(runtime->registry->find("link.get") != nullptr);
}

// Type: unit (runtime reuse / no per-request rebuild).
// Scenario: two request_runtime() calls with no process runtime installed
//   return the same CommandRuntime instance (pointer equality) — the fallback
//   is built once, not per call.
// Boundaries: the management plane shares one runtime (the fix for the
//   per-request rebuild that caused split brain). On failure first check: did
//   default_request_runtime stop caching the function-local static?
BOOST_AUTO_TEST_CASE(request_runtime_is_reused_across_calls)
{
    url_shortener::app::LinkBackend::reset();
    composition::reset_process_command_runtime();
    const auto first = request_runtime();
    const auto second = request_runtime();
    BOOST_REQUIRE(first != nullptr);
    BOOST_REQUIRE(second != nullptr);
    BOOST_TEST(first.get() == second.get());
}

// Type: unit (no split brain under an active persistent backend, the core fix).
// Scenario: with a SQLite-backed handle installed as the process runtime, a link
//   created through the bridge (link.create -> LinkBackend::store()) is visible
//   to link.list through the same process runtime — both read/write the one
//   installed store, not an empty legacy store.
// Boundaries: before the fix the bridge built a per-request runtime over an
//   inactive handle, so link.list read the empty global linkRepository() while
//   CRUD went to the SQLite store (split brain). On failure first check: did the
//   bridge stop using process_command_runtime, or did the active handle stop
//   selecting the RepositoryLinkLister?
BOOST_AUTO_TEST_CASE(process_runtime_link_list_sees_link_created_through_backend)
{
    const auto db = unique_temp_path(".db");
    const auto yaml = unique_temp_path(".yaml");
    std::filesystem::remove(db);
    write_file(yaml,
               "metadata:\n  backend: sqlite\n  sqlite_path: " + db.string()
                   + "\n");

    ServerConfig server;
    server.storage_config_path = yaml.string();

    url_shortener::app::LinkBackend::reset();
    composition::reset_process_command_runtime();

    auto handle = url_shortener::app::InitLinkBackend(server);
    BOOST_REQUIRE(handle.active());
    BOOST_REQUIRE(handle.repository() != nullptr);

    composition::install_process_command_runtime(
        std::make_shared<composition::CommandRuntime>(
            composition::build_command_runtime(server, handle)));

    const std::string slug = "bridge-sqlite-slug";
    const commands::CommandResponse created = execute_resource_command(
        server,
        "link.create",
        R"({"target_url":"https://example.net/x","slug":")" + slug + R"("})",
        "rid-create");
    BOOST_REQUIRE(created.ok);

    const commands::CommandResponse listed = execute_resource_command(
        server, "link.list", R"({"status":"all","limit":500})", "rid-list");
    BOOST_REQUIRE(listed.ok);
    BOOST_TEST(listed.result_json.find(slug) != std::string::npos);

    // Teardown: drop the runtime before the handle it borrows from, then restore
    // the process-wide defaults for any following case / executable.
    composition::reset_process_command_runtime();
    url_shortener::app::LinkBackend::reset();
    std::filesystem::remove(db);
    std::filesystem::remove(yaml);
}
