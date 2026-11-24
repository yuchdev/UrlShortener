#define BOOST_TEST_MODULE TuiRequiredPermissionForwardTest
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/security/control_set.hpp>
#include <url_shortener/tui/in_process_client.hpp>

using url_shortener::app::LinkBackend;
using url_shortener::app::LinkBackendHandle;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::Permission;
using url_shortener::composition::build_command_runtime;
using url_shortener::composition::CommandRuntime;
using url_shortener::tui::InProcessClient;

namespace
{

// Maps a base Permission to its wire string, mirroring the mapping the registry
// and the client use. The test asserts the client's required_permission equals
// this mapping of the registry's resolve_permission, so the client cannot be
// silently re-implementing the raise rule.
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

CommandRequest request(std::string command, std::string args_json)
{
    CommandRequest req;
    req.v = 1;
    req.id = "req-1";
    req.command = std::move(command);
    req.args_json = std::move(args_json);
    return req;
}

}  // namespace

// Type: unit (ICommandClient::required_permission forwards to the registry, C8).
// Scenario: with the real production registry (build_command_runtime) behind an
//   InProcessClient, required_permission() returns exactly
//   permission_name(registry.resolve_permission(request)) for every raising
//   case -- link.list status=active/all/deleted and link.export with/without a
//   path -- and the expected read/write value. The client never re-implements
//   the argument-dependent raise; it mirrors the registry's single source of
//   truth. This is the exact gap that produced the 2026-11-19 HIGH x2 finding
//   (arg-blind read-only mirror, c770499/ec1cfe7).
// Boundaries: the client holds a const reference to the registry and calls
//   resolve_permission; TUX UX gating (disabled all/deleted choices) is proven
//   by subtask 28, not here.
// On failure first check: did InProcessClient::required_permission stop
//   delegating to resolve_permission, or start applying its own rule?
BOOST_AUTO_TEST_CASE(in_process_client_required_permission_forwards)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = build_command_runtime(server, backend);
    const CommandRegistry& registry = *runtime.registry;

    url_shortener::commands::CallerContext caller;
    caller.principal = "local-operator";
    caller.transport = "tui";
    caller.control_set = ControlSet::Admin;
    InProcessClient client(registry, caller);

    struct Tuple
    {
        CommandRequest req;
        std::string expected;
    };
    const std::vector<Tuple> tuples = {
        {request("link.list", R"({"status":"active"})"), "read"},
        {request("link.list", R"({"status":"all"})"), "write"},
        {request("link.list", R"({"status":"deleted"})"), "write"},
        {request("link.export", R"({"format":"csv"})"), "read"},
        {request("link.export", R"({"format":"csv","path":"/tmp/o.csv"})"),
         "write"},
    };

    for (const Tuple& tuple : tuples) {
        const std::string via_client = client.required_permission(tuple.req);
        const std::string via_registry =
            permission_name(registry.resolve_permission(tuple.req));
        // The client is byte-equal to the registry's resolver...
        BOOST_TEST(via_client == via_registry,
                   "client/registry disagree for " << tuple.req.command);
        // ...and both report the expected raised permission.
        BOOST_TEST(via_client == tuple.expected,
                   "unexpected permission for " << tuple.req.command);
    }
}
