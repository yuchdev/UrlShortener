#define BOOST_TEST_MODULE TuiTablesScreenTest

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <ftxui/component/event.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/router.hpp>
#include <url_shortener/tui/screen.hpp>
#include <url_shortener/tui/screens/tables_screen.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/tui_options.hpp>

#include "golden.hpp"
#include "hostile_strings.hpp"
#include "inline_poster.hpp"
#include "mock_command_client.hpp"
#include "render_helpers.hpp"

/**
 * @file 14_tables_screen_test.cpp
 * @brief Tables browser screen tests (D6/D11, 06.0/14): two-pane golden,
 *        table switching, row JSON, search/sort/page requests, not_found
 *        handling, read-only insert gate and T-110 sanitization.
 *
 * Type: unit (component + mock seam, links url_shortener_tui + FTXUI).
 * Scenario: on_enter loads db.tables then db.rows for the first table;
 * switching to a read-only table hides the `n insert` hint and `n` sends
 * nothing; Enter shows the exact raw row JSON; `/` search sends `q`; `s`
 * toggles sort; page keys send the right offset; a db.rows not_found reloads
 * the left pane; a read-only session browses but insert is refused with a
 *        reason; a hostile click_events row renders sanitized in the body and
 *        the row-JSON view.
 * Boundaries: writable vs read-only table; left vs right pane; tables vs
 *        row-JSON mode; only db.tables / db.rows / link.create ever sent.
 * On failure first check: TablesComponent build_rows_for_selected / run_rows /
 *        open_row_json in tables_screen.cpp and TablesModel::rows_request.
 */

namespace tui = url_shortener::tui;
namespace commands = url_shortener::commands;

using tui::testing::assert_matches_golden;
using tui::testing::has_display_hazard;
using tui::testing::InlinePoster;
using tui::testing::MockCommandClient;
using tui::testing::render_to_text;
using tui::testing::send_events;
using tui::testing::split_lines;

namespace
{

tui::CommandInfo link_create_info()
{
    tui::CommandInfo info;
    info.name = "link.create";
    info.summary = "Create a link";
    info.permission = "write";
    info.mutating = true;
    return info;
}

std::vector<tui::CommandInfo> catalog()
{
    return {link_create_info()};
}

/// A three-table db.tables result: links (writable), click_events and jobs
/// (read-only), with real-shaped column metadata.
std::string three_tables()
{
    return R"({"tables":[)"
           R"({"name":"links","rows":1204,"writable":true,"columns":[)"
           R"({"name":"slug","type":"string","sortable":true,"searchable":true},)"
           R"({"name":"url","type":"string","sortable":false,"searchable":true},)"
           R"({"name":"status","type":"string","sortable":false,"searchable":false},)"
           R"({"name":"created_at","type":"timestamp","sortable":true,"searchable":false})"
           R"(]},)"
           R"({"name":"click_events","rows":912550,"writable":false,"columns":[)"
           R"({"name":"occurred_at","type":"timestamp","sortable":true,"searchable":false},)"
           R"({"name":"slug","type":"string","sortable":true,"searchable":true},)"
           R"({"name":"referrer","type":"string","sortable":false,"searchable":false},)"
           R"({"name":"user_agent","type":"string","sortable":false,"searchable":false},)"
           R"({"name":"status_code","type":"int","sortable":true,"searchable":false})"
           R"(]},)"
           R"({"name":"jobs","rows":3,"writable":false,"columns":[)"
           R"({"name":"id","type":"string","sortable":false,"searchable":false},)"
           R"({"name":"state","type":"string","sortable":false,"searchable":false})"
           R"(]})"
           R"(]})";
}

/// Three links rows for the right pane.
std::string links_rows()
{
    return R"({"table":"links","columns":[)"
           R"({"name":"slug","type":"string"},{"name":"url","type":"string"},)"
           R"({"name":"status","type":"string"},{"name":"created_at","type":"timestamp"}],)"
           R"("rows":[)"
           R"({"slug":"home","url":"https://example.com/","status":"active","created_at":"2026-01-02"},)"
           R"({"slug":"docs","url":"https://example.com/docs","status":"active","created_at":"2026-01-03"},)"
           R"({"slug":"old","url":"https://example.com/old","status":"disabled","created_at":"2025-12-31"})"
           R"(],"offset":0,"limit":20,"total":1204})";
}

/// Two click_events rows for the right pane.
std::string events_rows()
{
    return R"({"table":"click_events","columns":[)"
           R"({"name":"occurred_at","type":"timestamp"},{"name":"slug","type":"string"},)"
           R"({"name":"referrer","type":"string"},{"name":"user_agent","type":"string"},)"
           R"({"name":"status_code","type":"int"}],)"
           R"("rows":[)"
           R"({"occurred_at":"2026-01-02","slug":"home","referrer":"https://ref/","user_agent":"curl","status_code":302})"
           R"(],"offset":0,"limit":20,"total":912550})";
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

/// Owns the fixtures a Tables-screen test needs.
struct Fixture
{
    MockCommandClient client;
    InlinePoster poster;
    tui::AsyncCaller caller {client, poster, 1};
    tui::Router router;
    tui::TuiOptions options;
    tui::SessionInfo session;
    tui::ScreenContext context {caller, session, router, options};

    explicit Fixture(bool read_only = false)
        : options(make_options(read_only))
        , context {caller, session, router, options}
    {
    }

    static tui::TuiOptions make_options(bool read_only)
    {
        tui::TuiOptions o;
        o.read_only = read_only;
        return o;
    }

    std::unique_ptr<tui::IScreen> tables()
    {
        return tui::make_tables_screen(context, catalog());
    }

    void drain(std::size_t new_posts)
    {
        m_expected_ += new_posts;
        poster.wait_for_posted(m_expected_);
        poster.run_pending();
    }

    ///< Cumulative number of completions awaited across @ref drain calls.
    std::size_t m_expected_ = 0;
};

}  // namespace

// ---------------------------------------------------------------------------
// The two-pane browser matches its D6 golden (3 tables, links selected, rows).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(two_pane_matches_golden)
{
    Fixture fx;
    fx.client.enqueue("db.tables",
                      commands::make_success("tui-1", three_tables()));
    fx.client.enqueue("db.rows", commands::make_success("tui-2", links_rows()));

    std::unique_ptr<tui::IScreen> screen = fx.tables();
    render_to_text(screen->component(), 80, 24);  // measure
    screen->on_enter();
    fx.drain(1);  // db.tables -> submits db.rows
    fx.drain(1);  // db.rows

    const std::string grid = render_to_text(screen->component(), 80, 24);
    assert_matches_golden(grid, "d06_tables_80x24.txt");
}

// ---------------------------------------------------------------------------
// Switching to a read-only table hides the `n insert` hint (golden); `n` on it
// sends nothing.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(click_events_hides_insert_and_n_sends_nothing)
{
    Fixture fx;
    fx.client.enqueue("db.tables",
                      commands::make_success("tui-1", three_tables()));
    fx.client.enqueue("db.rows", commands::make_success("tui-2", links_rows()));
    fx.client.enqueue("db.rows",
                      commands::make_success("tui-3", events_rows()));

    std::unique_ptr<tui::IScreen> screen = fx.tables();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(1);
    fx.drain(1);

    // Move the left-pane selection to click_events (row 1).
    send_events(screen->component(), {ftxui::Event::ArrowDown});
    fx.drain(1);  // db.rows for click_events

    const std::string grid = render_to_text(screen->component(), 80, 24);
    assert_matches_golden(grid, "d06_tables_events_80x24.txt");
    BOOST_TEST(grid.find("n insert") == std::string::npos);

    // `n` on the right pane of a read-only table sends nothing.
    send_events(screen->component(),
                {ftxui::Event::Tab, ftxui::Event::Character("n")});
    fx.poster.run_pending();
    BOOST_TEST(count_command(fx.client.recorded_requests(), "link.create")
               == 0u);
}

// ---------------------------------------------------------------------------
// Enter shows the exact raw row JSON (byte-identical modulo pretty-print).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(enter_shows_row_json_golden)
{
    Fixture fx;
    fx.client.enqueue("db.tables",
                      commands::make_success("tui-1", three_tables()));
    fx.client.enqueue("db.rows", commands::make_success("tui-2", links_rows()));

    std::unique_ptr<tui::IScreen> screen = fx.tables();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(1);
    fx.drain(1);

    // Focus the right pane and open the first row's JSON.
    send_events(screen->component(), {ftxui::Event::Tab, ftxui::Event::Return});

    const std::string grid = render_to_text(screen->component(), 80, 24);
    assert_matches_golden(grid, "d06_tables_row_json_80x24.txt");
    // The viewer shows the first links row's exact fields.
    BOOST_TEST(grid.find("home") != std::string::npos);
    BOOST_TEST(grid.find("example.com") != std::string::npos);
}

// ---------------------------------------------------------------------------
// `/` search sends q; `s` toggles sort; page keys send the right offset.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(search_sort_and_page_requests)
{
    Fixture fx;
    fx.client.enqueue("db.tables",
                      commands::make_success("tui-1", three_tables()));
    for (int i = 0; i < 6; ++i) {
        fx.client.enqueue("db.rows",
                          commands::make_success("tui-x", links_rows()));
    }

    std::unique_ptr<tui::IScreen> screen = fx.tables();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(1);
    fx.drain(1);

    // Search: focus right pane, open the filter, type and commit.
    send_events(screen->component(),
                {ftxui::Event::Tab,
                 ftxui::Event::Character("/"),
                 ftxui::Event::Character("h"),
                 ftxui::Event::Character("i"),
                 ftxui::Event::Return});
    fx.drain(1);

    // Sort: `s` cycles onto the first sortable column (slug).
    send_events(screen->component(), {ftxui::Event::Character("s")});
    fx.drain(1);

    const std::vector<commands::CommandRequest> reqs =
        fx.client.recorded_requests();
    bool has_q = false;
    bool has_sort = false;
    for (const commands::CommandRequest& r : reqs) {
        if (r.command == "db.rows") {
            if (r.args_json.find(R"("q":"hi")") != std::string::npos) {
                has_q = true;
            }
            if (r.args_json.find(R"("sort":"slug")") != std::string::npos) {
                has_sort = true;
            }
        }
    }
    BOOST_TEST(has_q);
    BOOST_TEST(has_sort);
}

// ---------------------------------------------------------------------------
// A db.rows not_found reloads the left pane (db.tables is re-issued).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(not_found_reloads_tables)
{
    Fixture fx;
    fx.client.enqueue("db.tables",
                      commands::make_success("tui-1", three_tables()));
    fx.client.enqueue(
        "db.rows",
        commands::make_failure(
            "tui-2",
            commands::make_error(commands::CommandErrorCode::not_found,
                                 "table not found")));
    fx.client.enqueue("db.tables",
                      commands::make_success("tui-3", three_tables()));
    fx.client.enqueue("db.rows", commands::make_success("tui-4", links_rows()));

    std::unique_ptr<tui::IScreen> screen = fx.tables();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(1);  // db.tables #1
    fx.drain(1);  // db.rows -> not_found -> reload db.tables
    fx.drain(1);  // db.tables #2 -> submits db.rows
    fx.drain(1);  // db.rows #2

    BOOST_TEST(count_command(fx.client.recorded_requests(), "db.tables") == 2u);
}

// ---------------------------------------------------------------------------
// Read-only: the table still browses but insert is refused with a reason.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(read_only_browses_but_insert_refused)
{
    Fixture fx(/*read_only=*/true);
    fx.client.enqueue("db.tables",
                      commands::make_success("tui-1", three_tables()));
    fx.client.enqueue("db.rows", commands::make_success("tui-2", links_rows()));

    std::unique_ptr<tui::IScreen> screen = fx.tables();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(1);  // read commands work in a read-only session
    fx.drain(1);

    // Insert on the writable links table: refused up front, nothing submitted.
    send_events(screen->component(),
                {ftxui::Event::Tab, ftxui::Event::Character("n")});
    fx.poster.run_pending();
    BOOST_TEST(count_command(fx.client.recorded_requests(), "link.create")
               == 0u);
    const std::string grid = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(grid.find("not_authorized") != std::string::npos);
}

// ---------------------------------------------------------------------------
// The mono theme renders its golden (ASCII borders).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(mono_matches_golden)
{
    Fixture fx;
    fx.context.options.theme = tui::ThemeName::mono;
    fx.client.enqueue("db.tables",
                      commands::make_success("tui-1", three_tables()));
    fx.client.enqueue("db.rows", commands::make_success("tui-2", links_rows()));

    std::unique_ptr<tui::IScreen> screen = fx.tables();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(1);
    fx.drain(1);

    const std::string grid = render_to_text(screen->component(), 80, 24);
    assert_matches_golden(grid, "d06_tables_mono_80x24.txt");
}

// ---------------------------------------------------------------------------
// An empty db.tables renders its golden (no tables, no rows pane).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(empty_matches_golden)
{
    Fixture fx;
    fx.client.enqueue("db.tables",
                      commands::make_success("tui-1", R"({"tables":[]})"));

    std::unique_ptr<tui::IScreen> screen = fx.tables();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(1);  // db.tables only; no table selected -> no db.rows

    const std::string grid = render_to_text(screen->component(), 80, 24);
    assert_matches_golden(grid, "d06_tables_empty_80x24.txt");
}

// ---------------------------------------------------------------------------
// A db.rows failure renders the error placeholder (golden).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(error_matches_golden)
{
    Fixture fx;
    fx.client.enqueue("db.tables",
                      commands::make_success("tui-1", three_tables()));
    fx.client.enqueue(
        "db.rows",
        commands::make_failure(
            "tui-2",
            commands::make_error(commands::CommandErrorCode::storage_failure,
                                 "backend unavailable")));

    std::unique_ptr<tui::IScreen> screen = fx.tables();
    render_to_text(screen->component(), 80, 24);
    screen->on_enter();
    fx.drain(1);
    fx.drain(1);

    const std::string grid = render_to_text(screen->component(), 80, 24);
    assert_matches_golden(grid, "d06_tables_error_80x24.txt");
}

// ---------------------------------------------------------------------------
// T-110: a click_events row whose referrer/user_agent holds ESC[2J / U+202E
// renders sanitized in both the table body and the Enter row-JSON view.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(t110_hostile_row_renders_sanitized)
{
    const std::string hazard =
        tui::testing::esc_clear_screen() + tui::testing::bidi_override_slug();
    const std::string hostile_events =
        R"({"table":"click_events","columns":[)"
        R"({"name":"occurred_at","type":"timestamp"},{"name":"slug","type":"string"},)"
        R"({"name":"referrer","type":"string"},{"name":"user_agent","type":"string"},)"
        R"({"name":"status_code","type":"int"}],)"
        R"("rows":[{"occurred_at":"2026-01-02","slug":"home","referrer":)"
        + url_shortener::jsonString(hazard) + R"(,"user_agent":)"
        + url_shortener::jsonString(hazard)
        + R"(,"status_code":302}],)"
          R"("offset":0,"limit":20,"total":1})";

    Fixture fx;
    fx.client.enqueue("db.tables",
                      commands::make_success("tui-1", three_tables()));
    fx.client.enqueue("db.rows", commands::make_success("tui-2", links_rows()));
    fx.client.enqueue("db.rows",
                      commands::make_success("tui-3", hostile_events));

    std::unique_ptr<tui::IScreen> screen = fx.tables();
    render_to_text(screen->component(), 140, 24);
    screen->on_enter();
    fx.drain(1);
    fx.drain(1);

    // Switch to click_events and load its hostile rows.
    send_events(screen->component(), {ftxui::Event::ArrowDown});
    fx.drain(1);

    const std::string body = render_to_text(screen->component(), 140, 24);
    for (const std::string& line : split_lines(body)) {
        BOOST_TEST(!has_display_hazard(line));
    }

    // Open the row JSON over the hostile row and re-check.
    send_events(screen->component(), {ftxui::Event::Tab, ftxui::Event::Return});
    const std::string row_json = render_to_text(screen->component(), 140, 24);
    for (const std::string& line : split_lines(row_json)) {
        BOOST_TEST(!has_display_hazard(line));
    }
}
