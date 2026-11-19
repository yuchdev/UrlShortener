#define BOOST_TEST_MODULE TuiReadOnlyRenderTest

#include <memory>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <ftxui/dom/elements.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/frame_model.hpp>
#include <url_shortener/tui/frame_view.hpp>
#include <url_shortener/tui/router.hpp>
#include <url_shortener/tui/screen.hpp>
#include <url_shortener/tui/screen_id.hpp>
#include <url_shortener/tui/screens/generic_command_screen.hpp>
#include <url_shortener/tui/screens/import_export_screen.hpp>
#include <url_shortener/tui/screens/palette_screen.hpp>
#include <url_shortener/tui/screens/placeholder_screen.hpp>
#include <url_shortener/tui/tui_options.hpp>

#include "golden.hpp"
#include "hostile_strings.hpp"
#include "inline_poster.hpp"
#include "mock_command_client.hpp"
#include "render_helpers.hpp"

/**
 * @file 18_readonly_render_test.cpp
 * @brief Golden render tests for the read-only presentation (subtask 06.0/18):
 *        the D1 title badge and the "disabled, not hidden" write controls.
 *
 * Type: unit (render/snapshot, links url_shortener_tui + FTXUI).
 * Scenario: the D1 frame of a read-only session shows `user: local/User` and
 * the
 *           `[READ-ONLY]` badge (d01 golden); the generated `link.create` form
 *           renders its Create button dimmed (d05 golden); the Import/Export
 *           screen renders the import Start disabled (d07 golden); the Command
 *           Palette dims write entries and shows the read-only reason (shared
 *           d09 golden). T-110: a hostile backend label never reaches the grid
 *           even with the badge appended.
 * Boundaries: badge present vs absent; write control disabled vs enabled.
 * On failure first check: FrameModel::title's badge, or a write screen no
 * longer rendering its control disabled (regenerate with UPDATE_GOLDEN=1 and
 *           review the diff).
 */

namespace tui = url_shortener::tui;

using tui::testing::assert_matches_golden;
using tui::testing::has_display_hazard;
using tui::testing::hostile_samples;
using tui::testing::InlinePoster;
using tui::testing::MockCommandClient;
using tui::testing::render_to_text;
using tui::testing::split_lines;

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
                    std::vector<tui::ArgInfo> args)
{
    tui::CommandInfo info;
    info.name = name;
    info.summary = name + " summary";
    info.permission = permission;
    info.mutating = mutating;
    info.args = std::move(args);
    return info;
}

std::vector<tui::CommandInfo> catalog()
{
    return {
        mk("db.tables", "read", false, {}),
        mk("link.list", "read", false, {}),
        mk("link.get", "read", false, {arg("slug", true)}),
        mk("link.create", "write", true, {arg("target_url", true)}),
        mk("link.update", "write", true, {arg("slug", true)}),
        mk("link.delete", "write", true, {arg("slug", true)}),
        mk("link.import", "write", true, {}),
        mk("link.export", "read", false, {arg("path", false)}),
        mk("system.info", "read", false, {}),
    };
}

tui::SessionInfo user_session()
{
    tui::SessionInfo s;
    s.principal = "local-operator";
    s.transport = "tui";
    s.read_only = true;
    return s;
}

/// Owns the fixtures a read-only screen render needs.
struct Fixture
{
    MockCommandClient client;
    InlinePoster poster;
    tui::AsyncCaller caller {client, poster, 1};
    tui::Router router;
    tui::TuiOptions options;
    tui::SessionInfo session;
    tui::ScreenContext context {caller, session, router, options};

    Fixture()
    {
        options.read_only = true;
        session.read_only = true;
        context.options = options;
        context.session = session;
    }
};

// The measured-width views need one frame to learn their box; the goldens use
// the second (stable) frame.
std::string render_twice(const ftxui::Component& component, int w, int h)
{
    render_to_text(component, w, h);
    return render_to_text(component, w, h);
}

}  // namespace

// ---------------------------------------------------------------------------
// Success criterion 1: the D1 frame of a read-only session shows local/User and
// the READ-ONLY badge.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(frame_badge_matches_golden)
{
    // A short backend label leaves room for the full `[READ-ONLY]` badge plus
    // the trailing `[F1 Help]` within 80 columns (a long label truncates the
    // badge - an FTXUI title-width limitation shared with the D1 sketch).
    tui::FrameModel model(user_session(), "sqlite", true, "dark");
    const ftxui::Element frame =
        tui::render_frame(model,
                          tui::ScreenId::dashboard,
                          tui::placeholder_body("Dashboard: not implemented"),
                          {},
                          80,
                          24);
    const std::string grid = render_to_text(frame, 80, 24);

    BOOST_TEST(grid.find("user: local/User") != std::string::npos);
    BOOST_TEST(grid.find("[READ-ONLY]") != std::string::npos);
    BOOST_TEST(grid.find("local/Admin") == std::string::npos);
    assert_matches_golden(grid, "d01_frame_readonly_80x24.txt");
}

// ---------------------------------------------------------------------------
// The generated link.create form renders its Create button disabled in a
// read-only session (disabled, not hidden - ADR 0003 rule 5).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(new_link_form_disabled_matches_golden)
{
    Fixture fx;
    tui::GenericCommandConfig cfg;
    cfg.command = mk("link.create", "write", true, {arg("target_url", true)});
    std::unique_ptr<tui::IScreen> screen =
        tui::make_generic_command_screen(fx.context, std::move(cfg));

    const std::string grid = render_twice(screen->component(), 80, 24);
    BOOST_TEST(grid.find("Create") != std::string::npos);  // shown, not hidden
    BOOST_TEST(grid.find("command: link.create") != std::string::npos);
    BOOST_TEST(grid.find("not_authorized") != std::string::npos);  // reason
    assert_matches_golden(grid, "d05_new_link_readonly_80x24.txt");
}

// ---------------------------------------------------------------------------
// The Import/Export screen renders the import Start disabled in a read-only
// session.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(import_disabled_matches_golden)
{
    Fixture fx;
    std::unique_ptr<tui::IScreen> screen =
        tui::make_import_export_screen(fx.context, catalog());

    const std::string grid = render_twice(screen->component(), 80, 24);
    assert_matches_golden(grid, "d07_import_readonly_80x24.txt");
}

// ---------------------------------------------------------------------------
// The Command Palette dims write entries and shows the read-only reason (the
// golden is shared with subtask 11).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(palette_dimming_matches_golden)
{
    Fixture fx;
    std::unique_ptr<tui::IScreen> screen =
        tui::make_palette_screen(fx.context, catalog());

    const std::string grid = render_twice(screen->component(), 80, 24);
    BOOST_TEST(grid.find("read-only session") != std::string::npos);
    assert_matches_golden(grid, "d09_palette_readonly_80x24.txt");
}

// ---------------------------------------------------------------------------
// Review E: a 60-column backend label must not clip the badge - both
// `[READ-ONLY]` and `local/User` stay fully visible at 80 columns (only the
// backend label shrinks).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(long_label_does_not_clip_badge)
{
    const std::string long_label(60, 'x');
    tui::FrameModel model(user_session(), long_label, true, "dark");
    const ftxui::Element frame =
        tui::render_frame(model,
                          tui::ScreenId::dashboard,
                          tui::placeholder_body("Dashboard: not implemented"),
                          {},
                          80,
                          24);
    const std::string grid = render_to_text(frame, 80, 24);

    BOOST_TEST(grid.find("[READ-ONLY]") != std::string::npos);
    BOOST_TEST(grid.find("local/User") != std::string::npos);
    BOOST_TEST(grid.find("[F1 Help]") != std::string::npos);
}

// ---------------------------------------------------------------------------
// T-110: a hostile backend label never reaches the grid even with the badge
// appended to the read-only title.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hostile_label_never_reaches_readonly_frame)
{
    for (const auto& sample : hostile_samples()) {
        tui::FrameModel model(user_session(), sample.raw, true, "dark");
        const ftxui::Element frame = tui::render_frame(
            model,
            tui::ScreenId::dashboard,
            tui::placeholder_body("Dashboard: not implemented"),
            {},
            80,
            24);
        const std::string grid = render_to_text(frame, 80, 24);
        BOOST_TEST(grid.find('\x1b') == std::string::npos,
                   "ESC reached the grid for sample: " << sample.name);
        for (const std::string& line : split_lines(grid)) {
            BOOST_TEST(!has_display_hazard(line),
                       "display hazard in a cell for sample: " << sample.name);
        }
    }
}
