#define BOOST_TEST_MODULE TuiReadOnlyMatrixTest

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <ftxui/component/event.hpp>
#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/security/control_set.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/capability.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/confirm.hpp>
#include <url_shortener/tui/in_process_client.hpp>
#include <url_shortener/tui/mutation_flow.hpp>
#include <url_shortener/tui/router.hpp>
#include <url_shortener/tui/screen.hpp>
#include <url_shortener/tui/screens/dashboard_screen.hpp>
#include <url_shortener/tui/screens/generic_command_screen.hpp>
#include <url_shortener/tui/screens/import_export_screen.hpp>
#include <url_shortener/tui/screens/jobs_screen.hpp>
#include <url_shortener/tui/screens/link_detail_screen.hpp>
#include <url_shortener/tui/screens/links_screen.hpp>
#include <url_shortener/tui/screens/palette_screen.hpp>
#include <url_shortener/tui/screens/tables_screen.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/toast.hpp>
#include <url_shortener/tui/tui_options.hpp>

#include "inline_poster.hpp"
#include "mock_command_client.hpp"
#include "render_helpers.hpp"

/**
 * @file 18_readonly_matrix_test.cpp
 * @brief The cross-cutting read-only safety proof (subtask 06.0/18, threat
 *        model T-116, contract C7).
 *
 * Type: unit (drives the real screen factories over a recording mock seam, and
 *       a registry-backed InProcessClient for the server-side check; links
 *       url_shortener_tui + FTXUI).
 * Scenario: in a read-only session every real screen (Links, LinkDetail,
 *           Tables, Jobs, Dashboard, Import/Export, Palette) is created,
 * entered and driven through its full action-key battery - and the recording
 *           client records **not one** request whose command is not a `read`.
 *           The two mutation entry-points that are not a "screen" are exercised
 *           too: the Palette/generic raw-JSON (`Ctrl-R`) path and a no-arg
 *           mutating command both send zero write requests in read-only, while
 *           in a read/write session they still pass through `ConfirmPolicy`
 *           (confirm gates them; never straight to `AsyncCaller::submit`).
 *           Finally, a registry-backed `InProcessClient` as `ControlSet::User`
 *           denies a write with `not_authorized` even when the client-side
 *           guard is bypassed, and the derived toast shows it.
 * Boundaries: read-only vs read/write session; every screen x every action key;
 *           generated form vs raw-JSON vs no-arg command; client-side mirror vs
 *           server-side registry enforcement.
 * On failure first check: whether a screen submits a mutation around
 *           run_mutation / MutationFlow (the choke point), or a new descriptor
 *           lacks an accurate `permission` (fix at is_read_only_command /
 *           CapabilityModel, never per screen).
 */

namespace tui = url_shortener::tui;
namespace commands = url_shortener::commands;

using tui::testing::InlinePoster;
using tui::testing::MockCommandClient;
using tui::testing::render_to_text;
using tui::testing::send_events;

namespace
{

tui::ArgInfo arg(const std::string& name, bool required = false)
{
    tui::ArgInfo info;
    info.name = name;
    info.kind = tui::ArgKind::string;
    info.required = required;
    return info;
}

tui::CommandInfo mk(const std::string& name,
                    const std::string& permission,
                    bool mutating,
                    bool is_job,
                    std::vector<tui::ArgInfo> args)
{
    tui::CommandInfo info;
    info.name = name;
    info.summary = name + " summary";
    info.permission = permission;
    info.mutating = mutating;
    info.is_job = is_job;
    info.args = std::move(args);
    return info;
}

/// The broad catalog every screen resolves its commands from. `link.export`
/// keeps its base `read` permission (its path hook raises it server-side, D16);
/// everything that changes state is `write`.
std::vector<tui::CommandInfo> catalog()
{
    const tui::ArgInfo slug = arg("slug", true);
    const tui::ArgInfo id = arg("id", true);
    return {
        mk("link.list", "read", false, false, {}),
        mk("link.get", "read", false, false, {slug}),
        mk("link.stats", "read", false, false, {slug}),
        mk("link.preview", "read", false, false, {slug}),
        mk("link.create", "write", true, false, {arg("target_url", true)}),
        mk("link.update", "write", true, false, {slug}),
        mk("link.enable", "write", true, false, {slug}),
        mk("link.disable", "write", true, false, {slug}),
        mk("link.delete", "write", true, false, {slug}),
        mk("link.restore", "write", true, false, {slug}),
        mk("link.import", "write", true, true, {}),
        mk("link.export", "read", false, true, {arg("path", false)}),
        mk("db.tables", "read", false, false, {}),
        mk("db.rows", "read", false, false, {arg("table", true)}),
        mk("job.list", "read", false, false, {}),
        mk("job.get", "read", false, false, {id}),
        mk("job.cancel", "write", true, false, {id}),
        mk("system.info", "read", false, false, {}),
        mk("command.list", "read", false, false, {}),
    };
}

const tui::CommandInfo* find(const std::vector<tui::CommandInfo>& cat,
                             const std::string& name)
{
    for (const tui::CommandInfo& c : cat) {
        if (c.name == name) {
            return &c;
        }
    }
    return nullptr;
}

commands::CommandResponse ok(const std::string& result)
{
    return commands::make_success("tui-1", result);
}

/// Scripts a generous supply of read responses for every read command a screen
/// might issue on entry or refresh, so the screens have real data to drive.
void script_reads(MockCommandClient& client)
{
    const std::string links =
        R"({"items":[)"
        R"({"slug":"docs","url":"https://example.com/docs",)"
        R"("status":"active","stats":{"total_redirects":12}},)"
        R"({"slug":"old","url":"https://example.com/old",)"
        R"("status":"deleted","stats":{"total_redirects":1}})"
        R"(],"offset":0,"limit":20,"total":2})";
    const std::string tables = R"({"tables":["links","click_events","jobs"]})";
    const std::string rows = R"({"table":"links","columns":["slug","url"],)"
                             R"("rows":[["docs","https://example.com/docs"]],)"
                             R"("offset":0,"limit":50,"total":1})";
    const std::string jobs =
        R"({"items":[{"id":"j-1","type":"link.import","state":"succeeded"}]})";
    const std::string info =
        R"({"backend":{"metadata":"memory"},"persistent":false})";
    const std::string link =
        R"({"slug":"docs","url":"https://example.com/docs",)"
        R"("status":"active","enabled":true})";
    const std::string stats = R"({"total_redirects":5})";

    for (int i = 0; i < 20; ++i) {
        client.enqueue("link.list", ok(links));
        client.enqueue("db.tables", ok(tables));
        client.enqueue("db.rows", ok(rows));
        client.enqueue("job.list", ok(jobs));
        client.enqueue("system.info", ok(info));
        client.enqueue("link.get", ok(link));
        client.enqueue("link.stats", ok(stats));
        client.enqueue("link.preview", ok(R"({"body":"PREVIEW"})"));
        client.enqueue("command.list", ok(R"({"commands":[]})"));
    }
}

/// Every action/navigation key a screen might bind, plus the submit/confirm
/// chords - the full battery each read-only screen is driven through.
std::vector<ftxui::Event> action_battery()
{
    std::vector<ftxui::Event> events;
    const char* keys = "netducixprsgGakjhlyq";
    for (const char* k = keys; *k != '\0'; ++k) {
        events.push_back(ftxui::Event::Character(std::string(1, *k)));
        // After each action key, an Enter would confirm a dialog if one opened;
        // in read-only none ever opens, so this only proves nothing submits.
        events.push_back(ftxui::Event::Return);
    }
    events.push_back(ftxui::Event::CtrlS);  // form / inline submit
    events.push_back(ftxui::Event::Return);
    events.push_back(ftxui::Event::CtrlR);  // raw-JSON toggle
    events.push_back(ftxui::Event::Return);
    events.push_back(ftxui::Event::Character(" "));
    events.push_back(ftxui::Event::Tab);
    return events;
}

/// Asserts every recorded request names a command that resolves, in the shared
/// catalog, to a `read` permission - i.e. no write/migrate ever left the TUI.
void assert_only_reads(const MockCommandClient& client,
                       const std::vector<tui::CommandInfo>& cat)
{
    for (const commands::CommandRequest& request : client.recorded_requests()) {
        const tui::CommandInfo* info = find(cat, request.command);
        BOOST_TEST_REQUIRE(info != nullptr,
                           "a request named a command not in the catalog: "
                               << request.command);
        BOOST_TEST(tui::is_read_only_command(*info),
                   "a read-only session submitted a non-read command: "
                       << request.command);
    }
}

/// Owns the fixtures the screen factories need, wired for a read-only session.
struct Fixture
{
    MockCommandClient client;
    InlinePoster poster;
    tui::AsyncCaller caller {client, poster, 1};
    tui::Router router;
    tui::TuiOptions options;
    tui::SessionInfo session;
    tui::ScreenContext context {caller, session, router, options};

    explicit Fixture(bool read_only = true)
    {
        options.read_only = read_only;
        session.read_only = read_only;
        context.options = options;
        context.session = session;
    }

    void settle()
    {
        // Drain whatever reads the screen posted; several passes cover a screen
        // that fans out (Dashboard) or chains a refresh.
        for (int i = 0; i < 12; ++i) {
            poster.run_pending();
        }
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Success criterion 2: for every real screen, a read-only session entered and
// driven through the full key battery records zero non-read requests.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(every_screen_sends_no_write_in_read_only)
{
    struct ScreenCase
    {
        std::string name;
        std::string expected_initial_read;  // empty = none on entry
        std::function<std::unique_ptr<tui::IScreen>(Fixture&)> make;
    };

    const std::vector<tui::CommandInfo> cat = catalog();

    const std::vector<ScreenCase> cases = {
        {"links",
         "link.list",
         [&cat](Fixture& fx)
         { return tui::make_links_screen(fx.context, cat); }},
        {"link_detail",
         "link.get",
         [&cat](Fixture& fx)
         {
             fx.context.pending_slug = "docs";
             return tui::make_link_detail_screen(fx.context, cat);
         }},
        {"tables",
         "db.tables",
         [&cat](Fixture& fx)
         { return tui::make_tables_screen(fx.context, cat); }},
        {"jobs",
         "job.list",
         [&cat](Fixture& fx)
         { return tui::make_jobs_screen(fx.context, cat); }},
        {"dashboard",
         "",  // fans out to several reads; don't pin one
         [&cat](Fixture& fx)
         { return tui::make_dashboard_screen(fx.context, cat); }},
        {"import_export",
         "",  // submits only on action
         [&cat](Fixture& fx)
         { return tui::make_import_export_screen(fx.context, cat); }},
        {"palette",
         "",  // lists the catalog; issues nothing on entry
         [&cat](Fixture& fx)
         { return tui::make_palette_screen(fx.context, cat); }},
    };

    for (const ScreenCase& sc : cases) {
        BOOST_TEST_CONTEXT("screen=" << sc.name)
        {
            Fixture fx(/*read_only=*/true);
            script_reads(fx.client);

            std::unique_ptr<tui::IScreen> screen = sc.make(fx);
            screen->on_enter();
            fx.settle();
            render_to_text(screen->component(), 80, 24);  // measure
            render_to_text(screen->component(), 80, 24);  // stable frame

            send_events(screen->component(), action_battery());
            fx.settle();
            render_to_text(screen->component(), 80, 24);

            // No write/migrate ever reached the client, from any key or path.
            assert_only_reads(fx.client, cat);

            // Reads still work: the screen's entry read was issued.
            if (!sc.expected_initial_read.empty()) {
                bool saw = false;
                for (const commands::CommandRequest& r :
                     fx.client.recorded_requests())
                {
                    if (r.command == sc.expected_initial_read) {
                        saw = true;
                    }
                }
                BOOST_TEST(saw,
                           "expected the entry read "
                               << sc.expected_initial_read << " to be issued");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// T-116: the Palette raw-JSON path cannot submit a write in a read-only
// session, driven through the real palette -> generic command screen.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(palette_raw_json_write_sends_nothing_in_read_only)
{
    const std::vector<tui::CommandInfo> cat = catalog();
    Fixture fx(/*read_only=*/true);

    std::unique_ptr<tui::IScreen> palette =
        tui::make_palette_screen(fx.context, cat);
    render_to_text(palette->component(), 80, 24);

    // Filter to a write command and open it.
    for (char ch : std::string("link.create")) {
        send_events(palette->component(),
                    {ftxui::Event::Character(std::string(1, ch))});
    }
    send_events(palette->component(), {ftxui::Event::Return});
    render_to_text(palette->component(), 80, 24);

    // Raw-JSON path: toggle, clear, type a complete object, submit, confirm.
    send_events(palette->component(), {ftxui::Event::CtrlR});
    send_events(palette->component(),
                {ftxui::Event::Backspace, ftxui::Event::Backspace});
    for (char ch : std::string(R"({"target_url":"https://x"})")) {
        send_events(palette->component(),
                    {ftxui::Event::Character(std::string(1, ch))});
    }
    send_events(palette->component(),
                {ftxui::Event::CtrlS, ftxui::Event::Return});
    fx.settle();

    BOOST_TEST(fx.client.recorded_requests().empty());
    assert_only_reads(fx.client, cat);

    const std::string grid = render_to_text(palette->component(), 80, 24);
    BOOST_TEST(grid.find("not_authorized") != std::string::npos);
}

// ---------------------------------------------------------------------------
// T-116: a no-arg mutating generic command sends nothing in read-only, and in
// a read/write session it still passes ConfirmPolicy (confirm gates it; the
// request is not recorded until the confirm is accepted).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(no_arg_mutating_generic_read_only_and_confirm_gate)
{
    const tui::CommandInfo purge = mk("cache.purge", "write", true, false, {});

    // Read-only: Ctrl-S submits nothing.
    {
        Fixture fx(/*read_only=*/true);
        tui::GenericCommandConfig cfg;
        cfg.command = purge;
        std::unique_ptr<tui::IScreen> screen =
            tui::make_generic_command_screen(fx.context, std::move(cfg));
        render_to_text(screen->component(), 80, 24);
        send_events(screen->component(),
                    {ftxui::Event::CtrlS, ftxui::Event::Return});
        fx.settle();
        BOOST_TEST(fx.client.recorded_requests().empty());
    }

    // Read/write: Ctrl-S opens a confirm; the request is not sent until Enter.
    {
        Fixture fx(/*read_only=*/false);
        fx.client.enqueue("cache.purge", ok(R"({"ok":true})"));
        tui::GenericCommandConfig cfg;
        cfg.command = purge;
        std::unique_ptr<tui::IScreen> screen =
            tui::make_generic_command_screen(fx.context, std::move(cfg));
        render_to_text(screen->component(), 80, 24);

        send_events(screen->component(), {ftxui::Event::CtrlS});
        // ConfirmPolicy gates the mutation: nothing has been submitted yet.
        BOOST_TEST(fx.client.recorded_requests().empty());

        send_events(screen->component(), {ftxui::Event::Return});  // confirm
        fx.poster.wait_for_posted(1);
        fx.poster.run_pending();
        BOOST_TEST(fx.client.recorded_requests().size() == 1u);
    }
}

// ---------------------------------------------------------------------------
// Constraint C7 (server-side enforcement is the real guard): a registry-backed
// InProcessClient running as ControlSet::User denies a write with
// not_authorized even when the client-side read-only mirror is bypassed, and
// the toast MutationFlow derives shows it. Defense in depth over the mirror.
// ---------------------------------------------------------------------------
namespace
{

using commands::ArgsView;
using commands::CommandContext;
using commands::CommandDescriptor;
using commands::CommandRegistry;
using commands::CommandResult;
using commands::Permission;

/// A tiny registry with one read and one write command, mirroring the live
/// authorization rule the TUI only presents.
void populate(CommandRegistry& registry)
{
    CommandDescriptor list;
    list.name = "link.list";
    list.summary = "List links";
    list.permission = Permission::read;
    list.handler = [](CommandContext&, const ArgsView&)
    {
        CommandResult result;
        result.result_json = R"({"items":[]})";
        return result;
    };
    registry.add(std::move(list));

    CommandDescriptor create;
    create.name = "link.create";
    create.summary = "Create a link";
    create.permission = Permission::write;
    create.mutating = true;
    create.handler = [](CommandContext&, const ArgsView&)
    {
        CommandResult result;
        result.result_json = R"({"slug":"x"})";
        return result;
    };
    registry.add(std::move(create));
}

commands::CommandRequest request(const std::string& command,
                                 const std::string& args = "{}")
{
    commands::CommandRequest r;
    r.v = 1;
    r.id = "tui-1";
    r.command = command;
    r.args_json = args;
    return r;
}

}  // namespace

BOOST_AUTO_TEST_CASE(registry_denies_write_even_if_client_guard_bypassed)
{
    CommandRegistry registry;
    populate(registry);

    commands::CallerContext user;
    user.principal = "local-operator";
    user.transport = "tui";
    user.control_set = ControlSet::User;
    tui::InProcessClient client(registry, user);

    // The session is reported read-only, matching the caller's control set.
    BOOST_TEST(client.session().read_only);
    BOOST_TEST(tui::CapabilityModel(client.session()).badge() == "READ-ONLY");

    // Positive control: a read still succeeds as this user.
    BOOST_TEST(client.call(request("link.list")).ok);

    // Direct call: the registry (not the TUI) denies the write.
    const commands::CommandResponse denied =
        client.call(request("link.create"));
    BOOST_TEST(!denied.ok);
    BOOST_TEST(
        (denied.error.code == commands::CommandErrorCode::not_authorized));

    // End-to-end: push a MutationFlow whose client-side read-only guard is OFF
    // (a stale UI path). The write leaves the TUI, the server denies it, and
    // the derived toast carries the not_authorized code - proving C7 holds even
    // when the mirror is bypassed.
    InlinePoster poster;
    tui::AsyncCaller caller {client, poster, 1};
    ManualClock clock {std::chrono::system_clock::time_point {}};
    tui::ToastQueue toasts {clock};
    tui::ConfirmPolicy policy(/*assume_yes=*/true);  // no confirm friction
    tui::MutationFlow flow(
        tui::MutationDeps {caller,
                           policy,
                           toasts,
                           tui::theme_for(tui::ThemeName::dark),
                           /*read_only=*/false});

    const tui::CommandInfo info =
        mk("link.create", "write", true, false, {arg("target_url", true)});
    tui::run_mutation(flow,
                      request("link.create", R"({"target_url":"https://x"})"),
                      info,
                      nullptr);
    poster.wait_for_posted(1);
    poster.run_pending();

    BOOST_REQUIRE(!toasts.visible().empty());
    const tui::Toast& toast = toasts.visible().back();
    BOOST_TEST((toast.kind == tui::ToastKind::error));
    BOOST_TEST(toast.title.find("not_authorized") != std::string::npos);
}
