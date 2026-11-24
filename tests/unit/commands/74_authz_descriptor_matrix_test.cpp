#define BOOST_TEST_MODULE CommandsAuthzDescriptorMatrixTest
#include <algorithm>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/app/link_backend.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/composition/command_runtime.hpp"
#include "url_shortener/composition/link_backend_bootstrap.hpp"
#include "url_shortener/core/config.hpp"
#include "url_shortener/http/command_bridge.hpp"
#include "url_shortener/security/control_set.hpp"

using url_shortener::app::LinkBackend;
using url_shortener::app::LinkBackendHandle;
using url_shortener::commands::ArgsView;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandContext;
using url_shortener::commands::CommandDescriptor;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::CommandResult;
using url_shortener::commands::Permission;
using url_shortener::composition::build_command_runtime;
using url_shortener::composition::CommandRuntime;
using url_shortener::http::rest_policy_refusal;

namespace
{

// Builds a fresh production runtime over the default in-memory backend, so the
// matrix iterates the real descriptors the composition root registers (the
// single registry builder, CLAUDE.md contract C11) rather than a hand-rolled
// stand-in. Every choke point under test -- effective_permission,
// resolve_permission, the link.list/link.export raise hooks -- is the shipped
// production code, not a mock.
CommandRuntime fresh_runtime(const ServerConfig& server,
                             const LinkBackendHandle& backend)
{
    return build_command_runtime(server, backend);
}

CallerContext caller(ControlSet role)
{
    CallerContext c;
    c.principal = "test-principal";
    c.transport = "test";
    c.control_set = role;
    return c;
}

CommandRequest request(std::string command, std::string args_json = "{}")
{
    CommandRequest req;
    req.id = "client-1";
    req.command = std::move(command);
    req.args_json = std::move(args_json);
    return req;
}

// Maps a base Permission to the wire string the catalog and the TUI mirror use,
// so a descriptor's permission can be compared against the catalog legend
// without reaching into the registry's anonymous-namespace helper.
std::string permission_name(Permission permission)
{
    switch (permission) {
        case Permission::read:
            return "read";
        case Permission::write:
            return "write";
        case Permission::migrate:
            return "migrate";
        case Permission::manage_users:
            return "manage_users";
    }
    return "<unmapped>";
}

// Returns the `"perm":"..."` the command.list JSON advertises for `name`, or an
// empty string when the command or its perm field is absent. The catalog emits
// each object as `{"name":...,"summary":...,"perm":...}` in that fixed order, so
// the first `"perm":"` after a command's `"name":"<name>"` is that command's.
std::string catalog_perm(const std::string& json, const std::string& name)
{
    const std::string key = "\"name\":\"" + name + "\"";
    const std::size_t at = json.find(key);
    if (at == std::string::npos) {
        return {};
    }
    const std::string perm_key = "\"perm\":\"";
    const std::size_t p = json.find(perm_key, at);
    if (p == std::string::npos) {
        return {};
    }
    const std::size_t start = p + perm_key.size();
    const std::size_t end = json.find('"', start);
    if (end == std::string::npos) {
        return {};
    }
    return json.substr(start, end - start);
}

}  // namespace

// Type: unit (every descriptor carries an explicit, mapped permission, C7).
// Scenario: iterate the whole production registry; each descriptor's permission
//   is one of read/write/migrate/manage_users -- never an out-of-range value a
//   cast could smuggle past the permission_check switch (which fails closed).
// Boundaries: the registry is the real build_command_runtime one, so a newly
//   registered command is covered automatically.
// On failure first check: did a descriptor ship with a cast/garbage permission,
//   or did a new Permission enumerator appear without a mapping?
BOOST_AUTO_TEST_CASE(every_descriptor_has_explicit_permission)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = fresh_runtime(server, backend);

    const std::vector<const CommandDescriptor*> descriptors =
        runtime.registry->list();
    BOOST_REQUIRE(!descriptors.empty());
    for (const CommandDescriptor* descriptor : descriptors) {
        const Permission perm = descriptor->permission;
        const bool mapped = perm == Permission::read || perm == Permission::write
            || perm == Permission::migrate || perm == Permission::manage_users;
        BOOST_TEST(mapped,
                   "descriptor has an unmapped base permission: "
                       << descriptor->name);
    }
}

// Type: unit (the catalog legend equals the descriptor permission, C6).
// Scenario: command.list is rendered through the registry and, for every
//   descriptor, the advertised "perm" equals permission_name(descriptor
//   permission). Proves the catalog rendering path cannot drift from the
//   descriptor the executor authorizes against.
// Boundaries: command.list reports the BASE permission (link.list/link.export
//   advertise read; their argument raise is a runtime decision), so the base is
//   the right thing to compare.
// On failure first check: did descriptor_to_json start reporting a different
//   permission than the descriptor holds?
BOOST_AUTO_TEST_CASE(permission_matches_catalog)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = fresh_runtime(server, backend);

    const CommandResponse listed = runtime.registry->execute(
        caller(ControlSet::Admin), request("command.list"));
    BOOST_REQUIRE(listed.ok);

    for (const CommandDescriptor* descriptor : runtime.registry->list()) {
        const std::string advertised =
            catalog_perm(listed.result_json, descriptor->name);
        BOOST_TEST(advertised == permission_name(descriptor->permission),
                   "catalog perm mismatch for " << descriptor->name);
    }
}

// Type: unit (a mutating command requires write-or-higher, success criterion 1).
// Scenario: (a) every production descriptor that is `mutating` carries a base
//   permission other than read; (b) the registration guard itself rejects a
//   mutating command declared with read permission and no permission_for hook.
// Boundaries: a mutating read-base command is only valid WITH a raise hook
//   (D16); the guard lives at command_registry.cpp:497-499.
// On failure first check: did a mutating command drop its write permission, or
//   did is_valid_descriptor stop enforcing the write-or-higher rule?
BOOST_AUTO_TEST_CASE(mutating_implies_write_or_higher)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = fresh_runtime(server, backend);

    for (const CommandDescriptor* descriptor : runtime.registry->list()) {
        if (descriptor->mutating) {
            BOOST_TEST((descriptor->permission != Permission::read),
                       "mutating command with read permission: "
                           << descriptor->name);
        }
    }

    // The registration guard is the real enforcement point: a mutating command
    // with read base and no raise hook must be rejected at add().
    CommandRegistry local;
    CommandDescriptor bad;
    bad.name = "ns.bad_write";
    bad.summary = "a mutating command mislabelled read";
    bad.permission = Permission::read;
    bad.mutating = true;
    bad.handler = [](CommandContext&, const ArgsView&)
    {
        CommandResult result;
        result.result_json = "{}";
        return result;
    };
    BOOST_CHECK_THROW(local.add(std::move(bad)), std::invalid_argument);
}

// Type: unit (a read-only User is denied every non-read descriptor, T-11/T-116).
// Scenario: for every descriptor whose base permission is not read, a
//   CallerContext{User} with empty args gets not_authorized. Because
//   authorization precedes argument validation (T-08), empty args are enough:
//   the base-permission gate fires before the handler or the arg schema is
//   reached, so no "minimal valid args" are required.
// Boundaries: link.list/link.export have a read base (their write raise is
//   argument-dependent), so they are correctly excluded here and covered by the
//   raised-permission cases below.
// On failure first check: did a write/migrate descriptor become reachable by a
//   User, or did authorization stop preceding dispatch?
BOOST_AUTO_TEST_CASE(user_controlset_denied_all_non_read)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = fresh_runtime(server, backend);

    std::size_t checked = 0;
    for (const CommandDescriptor* descriptor : runtime.registry->list()) {
        if (descriptor->permission == Permission::read) {
            continue;
        }
        ++checked;
        const CommandResponse response = runtime.registry->execute(
            caller(ControlSet::User), request(descriptor->name));
        BOOST_TEST(!response.ok,
                   "User must be denied " << descriptor->name);
        BOOST_TEST((response.error.code == CommandErrorCode::not_authorized),
                   "expected not_authorized for " << descriptor->name);
    }
    BOOST_TEST(checked > 0u);
}

// Type: unit (an Admin clears the base-permission guard, T-81/T-115).
// Scenario: for every descriptor whose base permission is write or migrate, an
//   Admin executing it with empty args is never denied with not_authorized (the
//   outcome may be invalid_field, unsupported, etc. -- any non-authorization
//   result proves the guard let Admin through). No production write command
//   mutates with empty args (each requires a slug/target/payload), so this
//   touches no stored state.
// Boundaries: the admin control set holds read+write+migrate, so the guard
//   grants; later arg validation is a separate concern.
// On failure first check: did AccessGuard deny an Admin a write/migrate it
//   should hold?
BOOST_AUTO_TEST_CASE(admin_controlset_allowed_write_and_migrate)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = fresh_runtime(server, backend);

    for (const CommandDescriptor* descriptor : runtime.registry->list()) {
        if (descriptor->permission != Permission::write
            && descriptor->permission != Permission::migrate)
        {
            continue;
        }
        const CommandResponse response = runtime.registry->execute(
            caller(ControlSet::Admin), request(descriptor->name));
        BOOST_TEST((response.error.code != CommandErrorCode::not_authorized),
                   "Admin must clear the guard for " << descriptor->name);
    }
}

// Type: unit (manage_users is reserved until Milestone 0002, guard against
//   accidental registration).
// Scenario: no production descriptor declares the manage_users base permission.
//   When Milestone 0002's user-management commands land (0002 Task 07.0) this
//   case is updated to allowlist them; until then its purpose is to trip if a
//   manage_users command is registered where no transport yet exposes it.
// Boundaries: the registry is the real one; a stray manage_users command would
//   be visible here.
// On failure first check: did someone register a manage_users command ahead of
//   the session/auth work?
BOOST_AUTO_TEST_CASE(manage_users_reserved)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = fresh_runtime(server, backend);

    for (const CommandDescriptor* descriptor : runtime.registry->list()) {
        BOOST_TEST((descriptor->permission != Permission::manage_users),
                   "unexpected manage_users descriptor: " << descriptor->name);
    }
}

// Type: unit (REST refuses manage_users for every role/peer, T-61).
// Scenario: a stub manage_users descriptor run through the real REST transport
//   policy (rest_policy_refusal, the bridge's choke point) is refused with
//   not_authorized -- the refusal is by descriptor base permission alone, so it
//   holds regardless of the caller (including an Admin). A read/write descriptor
//   is NOT refused (rest_policy_refusal returns no refusal), proving the rule is
//   scoped to manage_users and does not block ordinary commands.
// Boundaries: the policy is a transport rule in src/http/command_bridge.cpp
//   (command_bridge.cpp:141), evaluated before execute; it never inspects a
//   control set.
// On failure first check: did rest_policy_refusal stop keying on the
//   manage_users base permission, or start inspecting the caller role?
BOOST_AUTO_TEST_CASE(rest_policy_refuses_manage_users)
{
    CommandRegistry registry;

    CommandDescriptor manage;
    manage.name = "users.create";
    manage.summary = "a reserved user-management command";
    manage.permission = Permission::manage_users;
    manage.mutating = true;
    manage.handler = [](CommandContext&, const ArgsView&)
    {
        CommandResult result;
        result.result_json = "{}";
        return result;
    };
    registry.add(std::move(manage));

    CommandDescriptor writer;
    writer.name = "thing.write";
    writer.summary = "an ordinary write command";
    writer.permission = Permission::write;
    writer.mutating = true;
    writer.handler = [](CommandContext&, const ArgsView&)
    {
        CommandResult result;
        result.result_json = "{}";
        return result;
    };
    registry.add(std::move(writer));

    // manage_users is refused, for every role/peer (the policy never sees one).
    const auto refusal = rest_policy_refusal(registry, "users.create", "id-1");
    BOOST_REQUIRE(refusal.has_value());
    BOOST_TEST(!refusal->ok);
    BOOST_TEST((refusal->error.code == CommandErrorCode::not_authorized));
    BOOST_TEST(refusal->id == "id-1");

    // An ordinary write command is not refused by the transport policy.
    BOOST_TEST(
        !rest_policy_refusal(registry, "thing.write", "id-2").has_value());
    // An unknown command is not refused by the policy (execute reports it).
    BOOST_TEST(!rest_policy_refusal(registry, "no.such", "id-3").has_value());
}

// Type: unit (link.list raises read->write by status at the descriptor, T-71).
// Scenario: the registry's own resolve_permission (the public read path the
//   client preflight mirrors) returns read for status=active, and write for
//   status=all and status=deleted -- the raise lives at the descriptor
//   (link_list_command.cpp:299 via link_status_permission), not only at the
//   execute boundary.
// Boundaries: resolve_permission validates args then evaluates the hook, so the
//   status enum must be valid for the hook to be reached.
// On failure first check: did link_status_permission stop raising all|deleted,
//   or did resolve_permission stop evaluating the hook?
BOOST_AUTO_TEST_CASE(resolve_permission_link_list_status_raises)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = fresh_runtime(server, backend);
    const CommandRegistry& registry = *runtime.registry;

    BOOST_TEST((registry.resolve_permission(
                    request("link.list", R"({"status":"active"})"))
                == Permission::read));
    BOOST_TEST((registry.resolve_permission(
                    request("link.list", R"({"status":"all"})"))
                == Permission::write));
    BOOST_TEST((registry.resolve_permission(
                    request("link.list", R"({"status":"deleted"})"))
                == Permission::write));
}

// Type: unit (link.export raises read->write by path, T-9/T-118).
// Scenario: resolve_permission returns write for an export directed to a file
//   (`path` present) and read for an inline export (no `path`, default active
//   status). The export `format` is required, so a valid format is supplied for
//   the hook to run on a validated view.
// Boundaries: the raise is at pair_commands.cpp:1063; resolve_permission is the
//   single source of truth the TUI preflight consumes.
// On failure first check: did the export hook stop raising on path, or did the
//   descriptor lose its permission_for?
BOOST_AUTO_TEST_CASE(resolve_permission_link_export_path_raises)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = fresh_runtime(server, backend);
    const CommandRegistry& registry = *runtime.registry;

    BOOST_TEST((registry.resolve_permission(
                    request("link.export", R"({"format":"csv"})"))
                == Permission::read));
    BOOST_TEST((registry.resolve_permission(request(
                    "link.export", R"({"format":"csv","path":"/tmp/out.csv"})"))
                == Permission::write));
}

// Type: unit (the preflight permission equals the enforced permission, D16).
// Scenario: for the raising commands, the permission resolve_permission reports
//   (what the TUI preflight mirrors) is exactly the permission execute enforces
//   (what authorizes the call), proving a single source of truth. For each
//   (command, args, expected) tuple: resolve_permission == expected; a User is
//   denied iff expected is write; link.list status=active is allowed for a User
//   (enforced read) and status=all is allowed for an Admin (enforced write).
//   link.list is used for the execute checks because it is sync, so no job is
//   submitted as a side effect of exercising the allow direction.
// Boundaries: resolve_permission and execute both route through the one
//   effective_permission helper (command_registry.cpp:98), so they cannot drift.
// On failure first check: did execute and resolve_permission diverge on a raise
//   (the exact 2026-11-19 HIGH x2 gap c770499/ec1cfe7)?
BOOST_AUTO_TEST_CASE(preflight_matches_executed_effective_permission)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = fresh_runtime(server, backend);
    const CommandRegistry& registry = *runtime.registry;

    struct Tuple
    {
        CommandRequest req;
        Permission expected;
    };
    const std::vector<Tuple> tuples = {
        {request("link.list", R"({"status":"active"})"), Permission::read},
        {request("link.list", R"({"status":"all"})"), Permission::write},
        {request("link.list", R"({"status":"deleted"})"), Permission::write},
        {request("link.export", R"({"format":"csv"})"), Permission::read},
        {request("link.export", R"({"format":"csv","path":"/tmp/o.csv"})"),
         Permission::write},
    };

    for (const Tuple& tuple : tuples) {
        // 1. The preflight resolves the raised permission.
        BOOST_TEST((registry.resolve_permission(tuple.req) == tuple.expected),
                   "resolve_permission mismatch for " << tuple.req.command);

        // 2. A User is denied by execute exactly when the resolved permission is
        // write (the deny direction never dispatches, so no job is submitted).
        const CommandResponse user_response =
            registry.execute(caller(ControlSet::User), tuple.req);
        const bool user_denied =
            !user_response.ok
            && user_response.error.code == CommandErrorCode::not_authorized;
        BOOST_TEST(user_denied == (tuple.expected == Permission::write),
                   "User enforcement disagrees with preflight for "
                       << tuple.req.command);
    }

    // 3. The allow direction for a raised write: an Admin executing
    // link.list status=all is authorized (the enforced permission is write and
    // Admin holds it), so the response is not a denial.
    const CommandResponse admin_all = registry.execute(
        caller(ControlSet::Admin), request("link.list", R"({"status":"all"})"));
    BOOST_TEST(admin_all.ok,
               "Admin must be allowed to list status=all (enforced write)");
}
