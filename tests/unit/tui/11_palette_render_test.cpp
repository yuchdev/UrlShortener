#define BOOST_TEST_MODULE TuiPaletteRenderTest

#include <memory>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <ftxui/component/event.hpp>

#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/router.hpp>
#include <url_shortener/tui/screen.hpp>
#include <url_shortener/tui/screens/palette_screen.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/tui_options.hpp>

#include "golden.hpp"
#include "hostile_strings.hpp"
#include "inline_poster.hpp"
#include "mock_command_client.hpp"
#include "render_helpers.hpp"

/**
 * @file 11_palette_render_test.cpp
 * @brief D9 render/snapshot tests for the Command Palette (06.0/11).
 *
 * Type: unit (render/snapshot + component events, links url_shortener_tui).
 * Scenario: the empty, `link.`-filtered and read-only layouts match their D9
 *           goldens; the query line reads `: link.[`; the selection marker
 *           moves with the arrows; a hostile command summary never reaches the
 *           grid (T-110).
 * Boundaries: empty vs filtered query; full vs read-only (dim) session.
 * On failure first check: render_list()/entry_row() in palette_screen.cpp
 *           (regenerate goldens with UPDATE_GOLDEN=1 and review), or a sink that
 *           stopped calling sanitize_for_terminal.
 */

namespace tui = url_shortener::tui;

using tui::testing::assert_matches_golden;
using tui::testing::has_display_hazard;
using tui::testing::hostile_samples;
using tui::testing::InlinePoster;
using tui::testing::MockCommandClient;
using tui::testing::render_to_text;
using tui::testing::send_events;
using tui::testing::split_lines;

namespace
{

tui::CommandInfo cmd(const std::string& name,
                     const std::string& summary,
                     const std::string& permission,
                     bool mutating = false,
                     bool is_job = false)
{
    tui::CommandInfo info;
    info.name = name;
    info.summary = summary;
    info.permission = permission;
    info.mutating = mutating;
    info.is_job = is_job;
    return info;
}

std::vector<tui::CommandInfo> catalog()
{
    return {
        cmd("link.create", "Create a short link", "write", true),
        cmd("link.get", "Fetch a link by slug or id", "read"),
        cmd("link.list", "List links (filter, page)", "read"),
        cmd("link.update", "Partially update a link", "write", true),
        cmd("link.delete", "Soft-delete a link", "write", true),
        cmd("link.import", "Import pairs (job)", "write", true, true),
        cmd("link.export", "Export pairs (job)", "read", false, true),
        cmd("db.tables", "List logical tables", "read"),
        cmd("system.info", "Server configuration", "read"),
    };
}

std::vector<ftxui::Event> type(const std::string& text)
{
    std::vector<ftxui::Event> events;
    for (char ch : text) {
        events.push_back(ftxui::Event::Character(std::string(1, ch)));
    }
    return events;
}

/// Owns the fixtures a palette render needs.
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

    std::unique_ptr<tui::IScreen> make()
    {
        return tui::make_palette_screen(context, catalog());
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// The empty-query palette matches its D9 golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(empty_palette_matches_golden)
{
    Fixture fx;
    std::unique_ptr<tui::IScreen> screen = fx.make();
    const std::string grid = render_to_text(screen->component(), 80, 24);
    assert_matches_golden(grid, "d09_palette_empty_80x24.txt");
}

// ---------------------------------------------------------------------------
// A `link.` filter shows only link commands, the query line reads `: link.[`,
// and the layout matches its golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(link_prefix_matches_golden)
{
    Fixture fx;
    std::unique_ptr<tui::IScreen> screen = fx.make();
    render_to_text(screen->component(), 80, 24);  // measure the box
    send_events(screen->component(), type("link."));
    const std::string grid = render_to_text(screen->component(), 80, 24);

    BOOST_TEST(grid.find(": link.[") != std::string::npos);
    BOOST_TEST(grid.find("db.tables") == std::string::npos);
    assert_matches_golden(grid, "d09_palette_link_prefix_80x24.txt");
}

// ---------------------------------------------------------------------------
// A read-only session shows write commands dim with the not_authorized reason
// and matches its golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(read_only_palette_matches_golden)
{
    Fixture fx(/*read_only=*/true);
    std::unique_ptr<tui::IScreen> screen = fx.make();
    const std::string grid = render_to_text(screen->component(), 80, 24);

    // The empty-query view is compacted, so the per-command reason is replaced
    // by an explicit read-only note (disabled command tokens are also dim).
    BOOST_TEST(grid.find("read-only session: write commands disabled")
               != std::string::npos);
    assert_matches_golden(grid, "d09_palette_readonly_80x24.txt");
}

// ---------------------------------------------------------------------------
// The selection marker starts on the first row and moves with ArrowDown.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(selection_marker_moves)
{
    Fixture fx;
    std::unique_ptr<tui::IScreen> screen = fx.make();
    const std::string first = render_to_text(screen->component(), 80, 24);
    // The first row (db.tables by name order) carries the '>' marker.
    BOOST_TEST(first.find(">db.tables") != std::string::npos);

    send_events(screen->component(), {ftxui::Event::ArrowDown});
    const std::string second = render_to_text(screen->component(), 80, 24);
    BOOST_TEST(second.find(">db.tables") == std::string::npos);
}

// ---------------------------------------------------------------------------
// T-110: a hostile command summary never reaches the rendered grid.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hostile_summary_never_reaches_grid)
{
    for (const auto& sample : hostile_samples()) {
        MockCommandClient client;
        InlinePoster poster;
        tui::AsyncCaller caller {client, poster, 1};
        tui::Router router;
        tui::TuiOptions options;
        tui::SessionInfo session;
        tui::ScreenContext context {caller, session, router, options};

        std::vector<tui::CommandInfo> commands = {
            cmd("link.evil", sample.raw, "read")};
        std::unique_ptr<tui::IScreen> screen =
            tui::make_palette_screen(context, commands);
        // Filter so the full-row view renders the hostile summary (the compact
        // empty-query view shows names only); both paths must stay clean.
        render_to_text(screen->component(), 80, 24);
        send_events(screen->component(), type("link"));
        const std::string grid = render_to_text(screen->component(), 80, 24);

        BOOST_TEST(grid.find('\x1b') == std::string::npos,
                   "ESC reached the grid for sample: " << sample.name);
        for (const std::string& line : split_lines(grid)) {
            BOOST_TEST(!has_display_hazard(line),
                       "display hazard for sample: " << sample.name);
        }
    }
}
