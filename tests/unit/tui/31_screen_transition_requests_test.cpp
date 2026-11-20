#define BOOST_TEST_MODULE TuiScreenTransitionRequestsTest

#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <ftxui/component/event.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/router.hpp>
#include <url_shortener/tui/screen.hpp>
#include <url_shortener/tui/screen_id.hpp>
#include <url_shortener/tui/screens/dashboard_screen.hpp>
#include <url_shortener/tui/screens/import_export_screen.hpp>
#include <url_shortener/tui/screens/jobs_screen.hpp>
#include <url_shortener/tui/screens/links_screen.hpp>
#include <url_shortener/tui/screens/palette_screen.hpp>
#include <url_shortener/tui/screens/tables_screen.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/tui_options.hpp>

#include "inline_poster.hpp"
#include "mock_command_client.hpp"
#include "render_helpers.hpp"
#include "settle.hpp"

/**
 * @file 31_screen_transition_requests_test.cpp
 * @brief ADR 0003 D11 screen state machine, transition by transition: every
 *        legal edge has a row and every row issues the requests the table
 *        promises (07.0/27, gap `tui__31`).
 *
 * Type: unit (the real registered tab screens built over one shared
 * @ref ScreenContext, exactly as `tui_entry`/`App::run` build them, plus the
 * pure @ref Router state machine; links url_shortener_tui + FTXUI).
 *
 * Scenario: the amendment of 2026-11-20 removed `App::currentScreen()` and any
 * headless `App` render, so a transition's *legality* is proven against the one
 * D11 edge oracle @ref Router::is_legal and its *effect* against
 * @ref MockCommandClient::recorded_requests - never a current-screen accessor
 * or a frame marker. The table literal below mirrors the D11 mermaid push edges
 * one row per edge with the requests each transition issues; the completeness
 * case asserts the table's push-edge set equals the Router's legal-edge set, so
 * a D11 edge added in router.cpp with no row here fails (success criterion 1).
 * Each request-bearing transition is then driven through the real screen
 * factory: entering a tab fires its `on_enter` reads, and the per-screen keys
 * (Enter/`n`/Start) fire their nested-screen reads, drained deterministically
 * with @ref settle_until_idle (no sleeps, no wall-clock).
 *
 * Boundaries: tab-switch edges (digits/`:`, handled by @ref
 * Router::navigate_tab) vs D11 push edges (@ref Router::push / `is_legal`); a
 * conditional edge
 * (`tables --n--> link_form` fires only on a writable table); an unmapped key.
 *
 * On failure first check: a D11 edge changed in `src/tui/router.cpp` without a
 * matching row in @ref k_transitions (the completeness case names the offending
 * pair), or a screen's `on_enter`/activation stopped issuing the documented
 * command (the per-screen request cases name it).
 *
 * The D11 push edges (mermaid, ADR 0003), kept adjacent to @ref k_transitions:
 *   Dashboard --> Links|Tables|ImportExport|Jobs|Palette
 *   Links --> LinkDetail|LinkForm
 *   LinkDetail --> LinkForm
 *   LinkForm --> Confirm
 *   Confirm --> Links|Dashboard
 *   Tables --> RowJson|LinkForm
 *   ImportExport --> Jobs
 *   Palette --> GenericForm
 *   GenericForm --> Confirm
 *   Jobs --> JobDetail
 */

namespace tui = url_shortener::tui;
namespace commands = url_shortener::commands;

using tui::ScreenId;
using tui::testing::MockCommandClient;
using tui::testing::render_to_text;
using tui::testing::send_events;
using tui::testing::settle_until_idle;

namespace
{

/// Builds a command-argument descriptor the slug-bearing reads resolve from.
tui::ArgInfo arg(const std::string& name,
                 tui::ArgKind kind = tui::ArgKind::string,
                 bool required = false)
{
    tui::ArgInfo info;
    info.name = name;
    info.kind = kind;
    info.required = required;
    return info;
}

/// Builds a catalog entry; `mutating` follows the permission like the registry.
tui::CommandInfo cmd(std::string name,
                     std::string permission,
                     bool is_job,
                     std::vector<tui::ArgInfo> args)
{
    tui::CommandInfo info;
    info.name = std::move(name);
    info.summary = info.name + " summary";
    info.permission = std::move(permission);
    info.is_job = is_job;
    info.mutating = info.permission != "read";
    info.args = std::move(args);
    return info;
}

/// A broad `link.*`/`db.*`/`job.*` catalog so every tab screen resolves the
/// commands its transitions reference (slug args included for the detail
/// reads).
std::vector<tui::CommandInfo> catalog()
{
    const tui::ArgInfo slug = arg("slug", tui::ArgKind::string, true);
    return {
        cmd("system.info", "read", false, {}),
        cmd("stats.summary", "read", false, {}),
        cmd("link.list", "read", false, {}),
        cmd("link.get", "read", false, {slug}),
        cmd("link.stats", "read", false, {slug, arg("bucket")}),
        cmd("link.preview", "read", false, {slug}),
        cmd("link.create",
            "write",
            false,
            {arg("target_url", tui::ArgKind::string, true), arg("slug")}),
        cmd("link.update", "write", false, {slug}),
        cmd("link.delete", "write", false, {slug}),
        cmd("link.enable", "write", false, {slug}),
        cmd("link.disable", "write", false, {slug}),
        cmd("link.restore", "write", false, {slug}),
        cmd("link.import", "write", true, {}),
        cmd("link.export", "read", true, {}),
        cmd("db.tables", "read", false, {}),
        cmd("db.rows", "read", false, {}),
        cmd("job.list", "read", false, {}),
        cmd("job.get", "read", false, {arg("id", tui::ArgKind::string, true)}),
        cmd("job.cancel", "write", false, {arg("id")}),
    };
}

commands::CommandResponse ok(const std::string& result)
{
    return commands::make_success("tui-x", result);
}

/// A one-row `link.list` page whose single row is selectable for Enter.
std::string one_link_page()
{
    return R"({"items":[)"
           R"({"slug":"docs","url":"https://example.com/docs",)"
           R"("status":"active","stats":{"total_redirects":12}})"
           R"(],"offset":0,"limit":20,"total":1})";
}

/// A full LinkView JSON (serializeLinkViewJson shape) for the detail
/// `link.get`.
std::string full_link()
{
    return R"({"id":"l-1","slug":"docs",)"
           R"("url":"https://example.com/docs",)"
           R"("short_url":"https://sho.rt/docs",)"
           R"("created_at":"2024-01-01T00:00:00Z",)"
           R"("updated_at":"2024-01-02T00:00:00Z","status":"active",)"
           R"("redirect_type":"permanent","tags":[],"metadata":{},)"
           R"("campaign":null})";
}

/// An empty range report for the detail `link.stats`.
std::string stats_empty()
{
    return R"({"slug":"docs","total_attempts":0,"successful_redirects":0,)"
           R"("attempts_by_status_code":{},"attempts_by_domain":{},)"
           R"("time_buckets":[],"from":0,"to":604800,"bucket":"day"})";
}

/// A three-table `db.tables` result: links (writable), click_events + jobs
/// (read-only), so the conditional `tables --n--> link_form` edge can be
/// probed.
std::string three_tables()
{
    return R"({"tables":[)"
           R"({"name":"links","rows":1,"writable":true,"columns":[)"
           R"({"name":"slug","type":"string","sortable":true,"searchable":true})"
           R"(]},)"
           R"({"name":"click_events","rows":9,"writable":false,"columns":[)"
           R"({"name":"slug","type":"string","sortable":true,"searchable":true})"
           R"(]})"
           R"(]})";
}

/// A `db.rows` page for the links table's right pane.
std::string links_rows()
{
    return R"({"table":"links","columns":[{"name":"slug","type":"string"}],)"
           R"("rows":[{"slug":"docs"}],"offset":0,"limit":20,"total":1})";
}

/// A `db.rows` page for the click_events table's right pane.
std::string events_rows()
{
    return R"({"table":"click_events","columns":[{"name":"slug","type":"string"}],)"
           R"("rows":[{"slug":"docs"}],"offset":0,"limit":20,"total":9})";
}

/// A one-row `job.list` page whose single job is selectable for Enter.
std::string one_job_page()
{
    return R"({"items":[)"
           R"({"id":"j-1","command":"link.import","state":"running",)"
           R"("progress":{"done":1,"total":2}})"
           R"(],"total":1})";
}

/// A `job.get` detail envelope for the Enter transition.
std::string job_detail()
{
    return R"({"id":"j-1","command":"link.import","state":"running",)"
           R"("progress":{"done":1,"total":2},)"
           R"("created_at":"09:00:00"})";
}

commands::CommandResponse import_job_accepted()
{
    commands::JobInfo job;
    job.id = "j-9";
    job.state = commands::JobState::queued;
    return commands::make_job_accepted("tui-x", job);
}

std::size_t count_command(const std::vector<commands::CommandRequest>& reqs,
                          const std::string& name)
{
    std::size_t n = 0;
    for (const commands::CommandRequest& r : reqs) {
        if (r.command == name) {
            ++n;
        }
    }
    return n;
}

/**
 * @brief Builds every real tab screen over one shared @ref ScreenContext, the
 * way `tui_entry` registers factories and `App::run` instantiates them.
 *
 * A single shared caller/poster/router/context backs all seven tab screens, so
 * a transition is driven on the same objects the app wires together; each test
 * builds a fresh fixture, so @ref MockCommandClient::recorded_requests reflects
 * only that test's transition. Nested screens (link_detail, row_json,
 * job_detail, link_form, confirm, generic_form) are hosted inline by their tab
 * screen, not registered here, exactly as in production.
 */
struct AppFixture
{
    MockCommandClient client;
    tui::testing::InlinePoster poster;
    tui::AsyncCaller caller {client, poster, 1};
    tui::Router router;
    tui::SessionInfo session;
    tui::TuiOptions options {make_options()};
    tui::ScreenContext context {caller, session, router, options};

    std::unique_ptr<tui::IScreen> dashboard;
    std::unique_ptr<tui::IScreen> links;
    std::unique_ptr<tui::IScreen> tables;
    std::unique_ptr<tui::IScreen> palette;
    std::unique_ptr<tui::IScreen> jobs;
    std::unique_ptr<tui::IScreen> import_export;

    AppFixture()
    {
        client.set_commands(catalog());
        const std::vector<tui::CommandInfo> cat = catalog();
        dashboard = tui::make_dashboard_screen(context, cat);
        links = tui::make_links_screen(context, cat);
        tables = tui::make_tables_screen(context, cat);
        palette = tui::make_palette_screen(context, cat);
        jobs = tui::make_jobs_screen(context, cat);
        import_export = tui::make_import_export_screen(context, cat);
    }

    /// A non-read-only dark theme so `theme_for` resolves and no gate
    /// interferes.
    static tui::TuiOptions make_options()
    {
        tui::TuiOptions o;
        o.theme = tui::ThemeName::dark;
        o.read_only = false;
        return o;
    }

    /// Drains the shared caller to true quiescence (no sleeps, no wall-clock).
    void settle() { settle_until_idle(caller, poster); }

    std::vector<commands::CommandRequest> requests() const
    {
        return client.recorded_requests();
    }
};

/**
 * @brief One row of the D11 transition table: the source screen, the key(s)
 * that drive it, the destination, whether the edge is a @ref Router::push edge
 * (as opposed to a global tab switch), and the requests the transition issues.
 */
struct Transition
{
    ScreenId from;  ///< The screen the transition starts on.
    const char* keys;  ///< The key(s) that drive it (doc/trace only).
    ScreenId to;  ///< The destination screen.
    bool push_edge;  ///< `true` for a D11 push edge, `false` for a tab switch.
    const char*
        sends;  ///< The requests the transition issues (doc/trace only).
};

/// The D11 transition table (mirrors the mermaid above). The `push_edge` column
/// is the completeness oracle: every `true` row is one @ref Router::is_legal
/// edge, and the set of them must equal the Router's legal-edge set. Tab-switch
/// rows (digits/`:`) ARE also legal push edges in @ref Router::is_legal, so
/// they carry `push_edge = true` as well; the distinction is only how
/// navigation reaches them at runtime (@ref Router::navigate_tab).
const Transition k_transitions[] = {
    {ScreenId::dashboard, "2", ScreenId::links, true, "link.list"},
    {ScreenId::dashboard, "3", ScreenId::tables, true, "db.tables, db.rows"},
    {ScreenId::dashboard, "4", ScreenId::import_export, true, "none"},
    {ScreenId::dashboard, "5", ScreenId::jobs, true, "job.list"},
    {ScreenId::dashboard,
     ":",
     ScreenId::palette,
     true,
     "none (catalog cached)"},
    {ScreenId::links,
     "Enter",
     ScreenId::link_detail,
     true,
     "link.get, link.stats"},
    {ScreenId::links, "n/e", ScreenId::link_form, true, "none (e prefetches)"},
    {ScreenId::link_detail, "e", ScreenId::link_form, true, "none"},
    {ScreenId::link_form, "submit", ScreenId::confirm, true, "none"},
    {ScreenId::confirm, "ok", ScreenId::links, true, "link.* then link.list"},
    {ScreenId::confirm,
     "ok",
     ScreenId::dashboard,
     true,
     "command then refresh"},
    {ScreenId::tables, "Enter", ScreenId::row_json, true, "none"},
    {ScreenId::tables, "n", ScreenId::link_form, true, "none"},
    {ScreenId::import_export, "Start", ScreenId::jobs, true, "link.import"},
    {ScreenId::palette, "pick", ScreenId::generic_form, true, "none"},
    {ScreenId::generic_form, "submit", ScreenId::confirm, true, "none"},
    {ScreenId::jobs, "Enter", ScreenId::job_detail, true, "job.get"},
};

}  // namespace

// ---------------------------------------------------------------------------
// Every push-edge row in the transition table is a legal D11 edge: the row's
// own legality claim agrees with the single oracle `Router::is_legal`.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(every_table_push_edge_is_legal)
{
    for (const Transition& t : k_transitions) {
        if (t.push_edge) {
            BOOST_TEST(tui::Router::is_legal(t.from, t.to),
                       "table edge " << static_cast<int>(t.from) << "->"
                                     << static_cast<int>(t.to) << " (keys "
                                     << t.keys << ") is not legal");
        }
    }
}

// ---------------------------------------------------------------------------
// Success criterion 1: the transition table's push-edge set EQUALS the Router's
// legal-edge set. A D11 edge added in router.cpp without a row here (or a row
// whose edge was removed) fails this, so the requests-per-transition table can
// never silently fall behind the state machine.
// On failure first check: the pair named in the message - add/remove a row in
// k_transitions to match src/tui/router.cpp.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(table_push_edges_match_router_edge_set_exactly)
{
    std::set<std::pair<ScreenId, ScreenId>> table_edges;
    for (const Transition& t : k_transitions) {
        if (t.push_edge) {
            table_edges.insert({t.from, t.to});
        }
    }

    for (ScreenId from : tui::all_screen_ids()) {
        for (ScreenId to : tui::all_screen_ids()) {
            const bool in_table =
                table_edges.count(std::make_pair(from, to)) != 0;
            const bool legal = tui::Router::is_legal(from, to);
            BOOST_TEST(in_table == legal,
                       "edge " << static_cast<int>(from) << "->"
                               << static_cast<int>(to) << " in_table="
                               << in_table << " router_legal=" << legal);
        }
    }
}

// ---------------------------------------------------------------------------
// Dashboard --2--> Links: entering the Links tab issues exactly one link.list
// (the destination screen's on_enter read). Driven through the real factory.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(links_entry_issues_link_list)
{
    AppFixture fx;
    fx.client.enqueue("link.list", ok(one_link_page()));

    fx.links->on_enter();
    fx.settle();

    BOOST_TEST(count_command(fx.requests(), "link.list") == 1u);
}

// ---------------------------------------------------------------------------
// Links --Enter--> LinkDetail: activating the selected row issues link.get and
// link.stats (the inline detail screen's on_enter reads). The link_detail edge
// is legal in the Router even though the detail is hosted inline (the D1 frame
// renders only tab screens), so legality is the Router's and the effect is the
// recorded requests - exactly the amended contract.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(links_enter_issues_get_and_stats)
{
    AppFixture fx;
    fx.client.enqueue("link.list", ok(one_link_page()));
    fx.client.enqueue("link.get", ok(full_link()));
    fx.client.enqueue("link.stats", ok(stats_empty()));

    fx.links->on_enter();
    fx.settle();
    render_to_text(fx.links->component(), 80, 24);  // establish selection

    send_events(fx.links->component(), {ftxui::Event::Return});
    fx.settle();

    const std::vector<commands::CommandRequest> reqs = fx.requests();
    BOOST_TEST(count_command(reqs, "link.get") == 1u);
    BOOST_TEST(count_command(reqs, "link.stats") == 1u);
    BOOST_TEST(tui::Router::is_legal(ScreenId::links, ScreenId::link_detail));
}

// ---------------------------------------------------------------------------
// Links --n--> LinkForm: opening the create form issues no request (the form is
// filled before any link.create). Legal edge, empty request set.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(links_new_opens_form_without_request)
{
    AppFixture fx;
    fx.client.enqueue("link.list", ok(one_link_page()));

    fx.links->on_enter();
    fx.settle();
    render_to_text(fx.links->component(), 80, 24);

    const std::size_t before = fx.requests().size();
    send_events(fx.links->component(), {ftxui::Event::Character("n")});
    fx.settle();

    const std::string grid = render_to_text(fx.links->component(), 80, 24);
    BOOST_TEST(grid.find("(command: link.create") != std::string::npos);
    BOOST_TEST(fx.requests().size() == before);  // no new request
    BOOST_TEST(tui::Router::is_legal(ScreenId::links, ScreenId::link_form));
}

// ---------------------------------------------------------------------------
// Dashboard --3--> Tables: entering the Tables tab issues db.tables then
// db.rows for the first table (the two-pane load). Driven through the real
// factory.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(tables_entry_issues_db_tables_then_rows)
{
    AppFixture fx;
    fx.client.enqueue("db.tables", ok(three_tables()));
    fx.client.enqueue("db.rows", ok(links_rows()));

    render_to_text(fx.tables->component(), 80, 24);  // measure
    fx.tables->on_enter();
    fx.settle();

    const std::vector<commands::CommandRequest> reqs = fx.requests();
    BOOST_TEST(count_command(reqs, "db.tables") == 1u);
    BOOST_TEST(count_command(reqs, "db.rows") == 1u);
}

// ---------------------------------------------------------------------------
// Tables --Enter--> RowJson: opening the raw-row view issues no request (the
// row is already loaded). Legal edge, no new request.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(tables_enter_opens_row_json_without_request)
{
    AppFixture fx;
    fx.client.enqueue("db.tables", ok(three_tables()));
    fx.client.enqueue("db.rows", ok(links_rows()));

    render_to_text(fx.tables->component(), 80, 24);
    fx.tables->on_enter();
    fx.settle();

    const std::size_t before = fx.requests().size();
    send_events(fx.tables->component(), {ftxui::Event::Return});
    fx.settle();

    BOOST_TEST(fx.requests().size() == before);
    BOOST_TEST(tui::Router::is_legal(ScreenId::tables, ScreenId::row_json));
}

// ---------------------------------------------------------------------------
// Conditional `tables --n--> link_form` edge: `n` on the read-only click_events
// table issues nothing (only the writable links table may insert), while the
// edge itself stays legal. The edge legality is unconditional (Router); the
// request is the screen's writable-table guard.
// On failure first check: the TablesComponent insert guard in
// tables_screen.cpp.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(n_on_non_writable_table_sends_nothing)
{
    AppFixture fx;
    fx.client.enqueue("db.tables", ok(three_tables()));
    fx.client.enqueue("db.rows", ok(links_rows()));
    fx.client.enqueue("db.rows", ok(events_rows()));

    render_to_text(fx.tables->component(), 80, 24);
    fx.tables->on_enter();
    fx.settle();

    // Move the left-pane selection to the read-only click_events table.
    send_events(fx.tables->component(), {ftxui::Event::ArrowDown});
    fx.settle();

    const std::size_t before = fx.requests().size();
    send_events(fx.tables->component(),
                {ftxui::Event::Tab, ftxui::Event::Character("n")});
    fx.settle();

    BOOST_TEST(count_command(fx.requests(), "link.create") == 0u);
    BOOST_TEST(fx.requests().size() == before);
    BOOST_TEST(tui::Router::is_legal(ScreenId::tables, ScreenId::link_form));
}

// ---------------------------------------------------------------------------
// Dashboard --5--> Jobs: entering the Jobs tab issues exactly one job.list.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(jobs_entry_issues_job_list)
{
    AppFixture fx;
    fx.client.enqueue("job.list", ok(one_job_page()));

    render_to_text(fx.jobs->component(), 80, 24);
    fx.jobs->on_enter();
    fx.settle();

    BOOST_TEST(count_command(fx.requests(), "job.list") == 1u);
}

// ---------------------------------------------------------------------------
// Jobs --Enter--> JobDetail: activating the selected job issues one job.get.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(jobs_enter_issues_job_get)
{
    AppFixture fx;
    fx.client.enqueue("job.list", ok(one_job_page()));
    fx.client.enqueue("job.get", ok(job_detail()));

    render_to_text(fx.jobs->component(), 80, 24);
    fx.jobs->on_enter();
    fx.settle();

    send_events(fx.jobs->component(), {ftxui::Event::Return});
    fx.settle();

    BOOST_TEST(count_command(fx.requests(), "job.get") == 1u);
    BOOST_TEST(tui::Router::is_legal(ScreenId::jobs, ScreenId::job_detail));
}

// ---------------------------------------------------------------------------
// Dashboard --4--> ImportExport: entering the Import/Export tab issues no
// request (it is an input form, not a data-backed screen).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(import_export_entry_sends_nothing)
{
    AppFixture fx;
    fx.import_export->on_enter();
    fx.settle();
    BOOST_TEST(fx.requests().empty());
}

// ---------------------------------------------------------------------------
// ImportExport --Start--> Jobs: a confirmed import issues exactly one
// link.import and hands the new job id forward for the Jobs tab to focus
// (context.focus_job_id), matching the D11 `ImportExport --> Jobs` edge.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(import_start_issues_link_import_and_hands_off)
{
    AppFixture fx;
    fx.client.enqueue("link.import", import_job_accepted());

    fx.import_export->on_enter();
    render_to_text(fx.import_export->component(), 80, 24);

    // Type a paste, Ctrl-S to confirm, Return to accept the confirm.
    send_events(fx.import_export->component(),
                {ftxui::Event::Character("https://example.com/a,a")});
    send_events(fx.import_export->component(), {ftxui::Event::CtrlS});
    send_events(fx.import_export->component(), {ftxui::Event::Return});
    fx.settle();

    BOOST_TEST(count_command(fx.requests(), "link.import") == 1u);
    BOOST_TEST(fx.context.focus_job_id == "j-9");
    BOOST_TEST(tui::Router::is_legal(ScreenId::import_export, ScreenId::jobs));
}

// ---------------------------------------------------------------------------
// Dashboard --:--> Palette: the palette is built from the already-fetched
// catalog (the in-process client answers `commands()` locally), so opening it
// issues no wire request. The `Palette --> GenericForm` pick edge stays legal.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(palette_entry_sends_no_wire_request)
{
    AppFixture fx;
    render_to_text(fx.palette->component(), 80, 24);
    fx.palette->on_enter();
    fx.settle();

    BOOST_TEST(fx.requests().empty());
    BOOST_TEST(
        tui::Router::is_legal(ScreenId::palette, ScreenId::generic_form));
}

// ---------------------------------------------------------------------------
// Dashboard overview: entering the Dashboard tab issues the batched overview
// reads (link.list, job.list, db.tables) in one refresh, with no response
// scripted - each unscripted call returns an internal-error envelope the
// dashboard renders as an error, so the batch still settles. This proves the
// `Dashboard` entry reads without coupling to the exact counts owned by
// `tui__17_dashboard_screen_test`.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(dashboard_entry_issues_overview_reads)
{
    AppFixture fx;
    fx.dashboard->on_enter();
    fx.settle();

    const std::vector<commands::CommandRequest> reqs = fx.requests();
    BOOST_TEST(count_command(reqs, "link.list") >= 1u);
    BOOST_TEST(count_command(reqs, "job.list") >= 1u);
    BOOST_TEST(count_command(reqs, "db.tables") >= 1u);
}

// ---------------------------------------------------------------------------
// An unmapped character on a data screen is ignored: no transition, no request.
// (Esc/`q`/Ctrl-C back/quit and the text-input focus rule are proven by
// `tui__20_app_navigation_test` and not duplicated here.)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(unmapped_key_issues_no_request)
{
    AppFixture fx;
    fx.client.enqueue("link.list", ok(one_link_page()));

    fx.links->on_enter();
    fx.settle();
    render_to_text(fx.links->component(), 80, 24);

    const std::size_t before = fx.requests().size();
    send_events(fx.links->component(), {ftxui::Event::Character("z")});
    fx.settle();

    BOOST_TEST(fx.requests().size() == before);
}
