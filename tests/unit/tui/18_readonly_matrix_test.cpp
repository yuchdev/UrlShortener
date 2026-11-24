#define BOOST_TEST_MODULE TuiReadOnlyMatrixTest

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
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
#include "settle.hpp"

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
using tui::testing::settle_until_idle;

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

/// Emulates the registry's `permission_for` raises (D16) against the mock, so
/// the arg-aware gate the screens consult sees `link.list {status:all|deleted}`
/// and `link.export {path|status:all|deleted}` as `write`.
std::optional<std::string> link_permission_hook(
    const commands::CommandRequest& request)
{
    const auto has = [&](const char* needle)
    { return request.args_json.find(needle) != std::string::npos; };
    if (request.command == "link.list") {
        if (has("\"status\":\"all\"") || has("\"status\":\"deleted\"")) {
            return std::string("write");
        }
        return std::string("read");
    }
    if (request.command == "link.export") {
        if (has("\"path\"") || has("\"status\":\"all\"")
            || has("\"status\":\"deleted\""))
        {
            return std::string("write");
        }
        return std::string("read");
    }
    return std::nullopt;  // fall back to the catalog base permission
}

/// Asserts every recorded request's *resolved effective permission* (arg-aware,
/// the registry's own rule emulated by the mock) is `read` - B review: assert
/// on the resolved permission of each request's args, not the command name.
void assert_only_reads(const MockCommandClient& client)
{
    for (const commands::CommandRequest& request : client.recorded_requests()) {
        BOOST_TEST(client.required_permission(request) == "read",
                   "a read-only session submitted a request whose resolved "
                   "permission is not read: "
                       << request.command << " args=" << request.args_json);
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

    explicit Fixture(bool read_only = true, bool assume_yes = false)
    {
        options.read_only = read_only;
        options.assume_yes = assume_yes;
        session.read_only = read_only;
        context.options = options;
        context.session = session;
        // The mock emulates the registry's argument-dependent permission raise,
        // so the screens' arg-aware gate resolves link.list/link.export exactly
        // as the server would (B review).
        client.set_commands(catalog());
        client.set_permission_hook(&link_permission_hook);
    }

    void settle()
    {
        // Drain whatever reads the screen posted, waiting for the real worker
        // to finish rather than pumping a fixed number of passes: under CPU
        // load a fixed loop can assert before an in-flight request is recorded,
        // which both flakes and weakens the read-only proof (a stray write
        // could slip past assert_only_reads). settle_until_idle loops until the
        // caller reports no outstanding work, covering a screen that fans out
        // (Dashboard) or chains a refresh.
        settle_until_idle(caller, poster);
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
        {"generic_form",
         "",  // a write command's generated form; D review adds it to the sweep
         [](Fixture& fx)
         {
             tui::GenericCommandConfig cfg;
             cfg.command = mk("link.create",
                              "write",
                              true,
                              false,
                              {arg("target_url", true)});
             return tui::make_generic_command_screen(fx.context,
                                                     std::move(cfg));
         }},
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
            assert_only_reads(fx.client);

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
// D review: `--yes` must not weaken the read-only guarantee. A read-only
// session that is also `--yes` (ConfirmPolicy collapses to no confirm) still
// submits zero non-read requests from the write-capable screens.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(assume_yes_does_not_bypass_read_only)
{
    const std::vector<tui::CommandInfo> cat = catalog();

    struct ScreenCase
    {
        std::string name;
        std::function<std::unique_ptr<tui::IScreen>(Fixture&)> make;
    };
    const std::vector<ScreenCase> cases = {
        {"links",
         [&cat](Fixture& fx)
         { return tui::make_links_screen(fx.context, cat); }},
        {"import_export",
         [&cat](Fixture& fx)
         { return tui::make_import_export_screen(fx.context, cat); }},
        {"palette",
         [&cat](Fixture& fx)
         { return tui::make_palette_screen(fx.context, cat); }},
        {"generic_form",
         [](Fixture& fx)
         {
             tui::GenericCommandConfig cfg;
             cfg.command = mk("link.create",
                              "write",
                              true,
                              false,
                              {arg("target_url", true)});
             return tui::make_generic_command_screen(fx.context,
                                                     std::move(cfg));
         }},
    };

    for (const ScreenCase& sc : cases) {
        BOOST_TEST_CONTEXT("screen=" << sc.name)
        {
            Fixture fx(/*read_only=*/true, /*assume_yes=*/true);
            script_reads(fx.client);

            std::unique_ptr<tui::IScreen> screen = sc.make(fx);
            screen->on_enter();
            fx.settle();
            render_to_text(screen->component(), 80, 24);
            render_to_text(screen->component(), 80, 24);
            send_events(screen->component(), action_battery());
            fx.settle();

            assert_only_reads(fx.client);
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
    assert_only_reads(fx.client);

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

using commands::ArgSpec;
using commands::ArgsView;
using commands::ArgType;
using commands::CommandContext;
using commands::CommandDescriptor;
using commands::CommandRegistry;
using commands::CommandResult;
using commands::Permission;

/// Records every (request, response) pair while delegating to a real client, so
/// a registry-backed drive can assert on the server's actual answers (B
/// review).
class RecordingClient final : public tui::ICommandClient
{
public:
    explicit RecordingClient(tui::ICommandClient& inner)
        : m_inner_(inner)
    {
    }

    commands::CommandResponse call(
        const commands::CommandRequest& request) override
    {
        commands::CommandResponse response = m_inner_.call(request);
        std::lock_guard<std::mutex> lock(m_mutex_);
        m_requests_.push_back(request);
        m_responses_.push_back(response);
        return response;
    }

    std::vector<tui::CommandInfo> commands() const override
    {
        return m_inner_.commands();
    }

    tui::SessionInfo session() const override { return m_inner_.session(); }

    std::string required_permission(
        const commands::CommandRequest& request) const override
    {
        return m_inner_.required_permission(request);
    }

    std::vector<commands::CommandResponse> responses() const
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        return m_responses_;
    }

    std::vector<commands::CommandRequest> requests() const
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        return m_requests_;
    }

private:
    tui::ICommandClient& m_inner_;
    mutable std::mutex m_mutex_;
    std::vector<commands::CommandRequest> m_requests_;
    std::vector<commands::CommandResponse> m_responses_;
};

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

ArgSpec opt_str(const std::string& name)
{
    ArgSpec spec;
    spec.name = name;
    spec.type = ArgType::string;
    spec.required = false;
    return spec;
}

ArgSpec opt_int(const std::string& name)
{
    ArgSpec spec;
    spec.name = name;
    spec.type = ArgType::integer;
    spec.required = false;
    return spec;
}

/// The argument-dependent raise the real link.list / link.export share (D16):
/// status=all|deleted (and, for export, a path) raise the base read to write.
Permission status_raise(const ArgsView& args)
{
    if (const std::optional<std::string> status = args.get_string("status")) {
        if (*status == "all" || *status == "deleted") {
            return Permission::write;
        }
    }
    if (args.get_string("path").has_value()) {
        return Permission::write;
    }
    return Permission::read;
}

CommandResult ok_result(const char* json)
{
    CommandResult result;
    result.result_json = json;
    return result;
}

/// A registry with the read commands the Dashboard, Links and Export screens
/// issue, each carrying the real argument-dependent permission raise, so an
/// InProcessClient(User) answers exactly as production would.
void populate_full(CommandRegistry& registry)
{
    CommandDescriptor list;
    list.name = "link.list";
    list.summary = "List links";
    list.permission = Permission::read;
    list.permission_for = status_raise;
    list.args = {opt_str("status"),
                 opt_str("q"),
                 opt_str("tag"),
                 opt_str("campaign"),
                 opt_str("sort"),
                 opt_str("order"),
                 opt_int("offset"),
                 opt_int("limit")};
    list.handler = [](CommandContext&, const ArgsView&)
    { return ok_result(R"({"items":[],"offset":0,"limit":20,"total":0})"); };
    registry.add(std::move(list));

    CommandDescriptor exp;
    exp.name = "link.export";
    exp.summary = "Export pairs";
    exp.permission = Permission::read;
    exp.permission_for = status_raise;
    exp.args = {opt_str("format"),
                opt_str("status"),
                opt_str("q"),
                opt_str("tag"),
                opt_str("campaign"),
                opt_str("path")};
    exp.handler = [](CommandContext&, const ArgsView&)
    { return ok_result(R"({"exported":0})"); };
    registry.add(std::move(exp));

    CommandDescriptor tables;
    tables.name = "db.tables";
    tables.summary = "List tables";
    tables.permission = Permission::read;
    tables.handler = [](CommandContext&, const ArgsView&)
    { return ok_result(R"({"tables":["links"]})"); };
    registry.add(std::move(tables));

    CommandDescriptor jobs;
    jobs.name = "job.list";
    jobs.summary = "List jobs";
    jobs.permission = Permission::read;
    jobs.args = {opt_str("state")};
    jobs.handler = [](CommandContext&, const ArgsView&)
    { return ok_result(R"({"items":[]})"); };
    registry.add(std::move(jobs));

    CommandDescriptor info;
    info.name = "system.info";
    info.summary = "System info";
    info.permission = Permission::read;
    info.handler = [](CommandContext&, const ArgsView&)
    { return ok_result(R"({"persistent":true})"); };
    registry.add(std::move(info));
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
    // A non-read-only capability: the client-side guard is deliberately OFF so
    // the write reaches the server, which must still deny it (C7).
    tui::SessionInfo bypass;
    bypass.read_only = false;
    tui::MutationFlow flow(
        tui::MutationDeps {caller,
                           policy,
                           toasts,
                           tui::theme_for(tui::ThemeName::dark),
                           tui::CapabilityModel(bypass)});

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

// ---------------------------------------------------------------------------
// B review (end-to-end): drive the real Dashboard, Links and Export screens
// read-only over a registry-backed InProcessClient(ControlSet::User) and assert
// the server never answers not_authorized and no recorded request resolves to a
// non-read permission. This FAILS on cdfacd1, where the default Links view
// (status=all), the Dashboard total/deleted counts and the default Export view
// all sent requests the registry raised to write and refused.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(registry_backed_screens_issue_no_raised_requests)
{
    CommandRegistry registry;
    populate_full(registry);

    commands::CallerContext user;
    user.principal = "local-operator";
    user.transport = "tui";
    user.control_set = ControlSet::User;
    tui::InProcessClient inner(registry, user);

    const std::vector<tui::CommandInfo> cat = inner.commands();

    struct ScreenCase
    {
        std::string name;
        std::function<std::unique_ptr<tui::IScreen>(tui::ScreenContext&)> make;
    };
    const std::vector<ScreenCase> cases = {
        {"links",
         [&cat](tui::ScreenContext& ctx)
         { return tui::make_links_screen(ctx, cat); }},
        {"dashboard",
         [&cat](tui::ScreenContext& ctx)
         { return tui::make_dashboard_screen(ctx, cat); }},
        {"import_export",
         [&cat](tui::ScreenContext& ctx)
         { return tui::make_import_export_screen(ctx, cat); }},
    };

    for (const ScreenCase& sc : cases) {
        BOOST_TEST_CONTEXT("screen=" << sc.name)
        {
            RecordingClient client(inner);
            InlinePoster poster;
            tui::AsyncCaller caller {client, poster, 1};
            tui::Router router;
            tui::TuiOptions options;
            options.read_only = true;
            tui::SessionInfo session = inner.session();  // read_only == true
            tui::ScreenContext context {caller, session, router, options};

            std::unique_ptr<tui::IScreen> screen = sc.make(context);
            screen->on_enter();
            settle_until_idle(caller, poster);
            render_to_text(screen->component(), 80, 24);
            render_to_text(screen->component(), 80, 24);
            send_events(screen->component(), action_battery());
            settle_until_idle(caller, poster);

            // The server never refused a request (no raised request was sent).
            for (const commands::CommandResponse& response : client.responses())
            {
                BOOST_TEST((response.error.code
                            != commands::CommandErrorCode::not_authorized),
                           "a read-only screen drew a not_authorized answer");
            }
            // Every recorded request resolves to a read permission.
            for (const commands::CommandRequest& request : client.requests()) {
                BOOST_TEST(inner.required_permission(request) == "read",
                           "a read-only screen sent a request resolving to a "
                           "non-read permission: "
                               << request.command
                               << " args=" << request.args_json);
            }
        }
    }
}
