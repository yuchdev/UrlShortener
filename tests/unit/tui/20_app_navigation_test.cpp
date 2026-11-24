#define BOOST_TEST_MODULE TuiAppNavigationTest

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <ftxui/component/event.hpp>
#include <url_shortener/tui/app_navigation.hpp>
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
#include <url_shortener/tui/screens/placeholder_screen.hpp>
#include <url_shortener/tui/screens/settings_screen.hpp>
#include <url_shortener/tui/screens/tables_screen.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/tui_options.hpp>

#include "inline_poster.hpp"
#include "mock_command_client.hpp"

/**
 * @file 20_app_navigation_test.cpp
 * @brief App-level navigation regression tests (BUG-2, 06.0/20 smoke).
 *
 * Type: unit (App navigation primitives over the real registered screens on one
 * shared @ref ScreenContext, links url_shortener_tui + FTXUI).
 * Scenario: the real screens are built exactly as `tui_entry`/`App::run` build
 * them - all seven real factories plus an Analytics placeholder - over one
 * shared context. Driving the extracted `sync_active_screen` /
 * `handle_global_key` (the exact logic `App::run` runs in its FTXUI lambdas)
 * proves: (1) no screen's constructor leaks `text_input_active`; (2) digit/`:`
 * hotkeys switch tabs from launch; (3) a flag left set by an off-screen screen
 * is cleared on tab entry; (4) digits typed while an Input is focused do not
 * navigate; (5) Ctrl-C reports a clean exit request.
 * Boundaries: off-screen vs active screen's text focus; navigation key with the
 * text-input guard clear vs set.
 * On failure first check: ImportExportComponent's constructor
 * (compute_default_focus must not write the shared context) and
 * `sync_active_screen` / `handle_global_key` in app_navigation.cpp.
 *
 * Regression: before the fix the Import/Export constructor set the shared
 * `text_input_active=true` at startup (its default focus is the paste area),
 * which dead-locked every global hotkey from launch; F1/`?`/Ctrl-C still
 * worked.
 */

namespace tui = url_shortener::tui;
namespace commands = url_shortener::commands;

using tui::testing::InlinePoster;
using tui::testing::MockCommandClient;

namespace
{

/// A broad catalog so every real screen resolves the commands it references at
/// construction (the gate only matters for the read-only matrix, not here).
std::vector<tui::CommandInfo> catalog()
{
    const auto make = [](std::string name, std::string permission, bool job)
    {
        tui::CommandInfo info;
        info.name = std::move(name);
        info.permission = std::move(permission);
        info.is_job = job;
        info.mutating = info.permission != "read";
        return info;
    };
    return {
        make("system.info", "read", false),
        make("stats.summary", "read", false),
        make("link.list", "read", false),
        make("link.create", "write", false),
        make("link.update", "write", false),
        make("link.delete", "write", false),
        make("link.import", "write", true),
        make("link.export", "read", true),
        make("db.list", "read", false),
        make("db.rows", "read", false),
        make("job.list", "read", false),
        make("job.cancel", "write", false),
    };
}

/**
 * @brief Builds the real App screen set over one shared @ref ScreenContext, the
 * way `tui_entry` registers factories and `App::run` instantiates them (plus
 * the Analytics placeholder `App::run` fills for the unimplemented tab).
 */
struct AppNavFixture
{
    MockCommandClient client;
    InlinePoster poster;
    tui::AsyncCaller caller {client, poster, 1};
    tui::Router router;
    tui::SessionInfo session;
    tui::TuiOptions options {make_options()};
    tui::ScreenContext context {caller, session, router, options};
    std::vector<std::unique_ptr<tui::IScreen>> screens;
    std::optional<tui::ScreenId> entered_tab;
    bool help_visible = false;

    AppNavFixture()
    {
        client.set_commands(catalog());
        client.set_session(session);
        build_screens();
    }

    /// A theme so `theme_for` resolves; not read-only so no gate interferes.
    static tui::TuiOptions make_options()
    {
        tui::TuiOptions o;
        o.theme = tui::ThemeName::dark;
        o.read_only = false;
        return o;
    }

    /// Mirrors `tui_entry`'s factory registration and `App::run`'s placeholder
    /// fill: all seven real screens plus an Analytics placeholder.
    void build_screens()
    {
        const std::vector<tui::CommandInfo> cat = catalog();
        screens.push_back(tui::make_dashboard_screen(context, cat));
        screens.push_back(tui::make_settings_screen(context));
        screens.push_back(tui::make_links_screen(context, cat));
        screens.push_back(tui::make_tables_screen(context, cat));
        screens.push_back(tui::make_palette_screen(context, cat));
        screens.push_back(tui::make_jobs_screen(context, cat));
        screens.push_back(tui::make_import_export_screen(context, cat));
        screens.push_back(tui::make_placeholder_screen(tui::ScreenId::analytics,
                                                       "Analytics"));
    }

    /// Dispatches one character key through the shared global handler.
    tui::GlobalKeyOutcome press(char key)
    {
        return tui::handle_global_key(
            ftxui::Event::Character(std::string(1, key)),
            router,
            context,
            help_visible);
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// BUG-2 constructor leak: building every real screen over one shared context
// must not set text_input_active (the Import/Export default focus is a text
// area, but its constructor must compute focus without touching the shared
// flag). On failure first check: ImportExportComponent's constructor calling
// reset_focus() instead of compute_default_focus().
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(building_all_screens_does_not_leak_text_input_focus)
{
    AppNavFixture fx;
    BOOST_TEST(!fx.context.text_input_active);
}

// ---------------------------------------------------------------------------
// BUG-2 main: at launch (Dashboard active) digit/`:` keys switch tabs. This is
// the exact smoke repro: before the fix the leaked flag made handle_global_key
// fall through (handled == false) and the view never left Dashboard.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(digit_and_palette_keys_switch_tabs_at_launch)
{
    AppNavFixture fx;

    // Enter the initial Dashboard tab exactly as the first render frame does.
    const int idx = tui::sync_active_screen(
        fx.screens, fx.router, fx.context, fx.entered_tab);
    BOOST_TEST(idx >= 0);
    BOOST_TEST((fx.router.active_tab() == tui::ScreenId::dashboard));
    BOOST_TEST(!fx.context.text_input_active);

    const struct
    {
        char key;
        tui::ScreenId expected;
    } cases[] = {
        {'3', tui::ScreenId::tables},
        {'2', tui::ScreenId::links},
        {'4', tui::ScreenId::import_export},
        {'5', tui::ScreenId::jobs},
        {'7', tui::ScreenId::settings},
        {':', tui::ScreenId::palette},
        {'1', tui::ScreenId::dashboard},
    };
    for (const auto& c : cases) {
        const tui::GlobalKeyOutcome outcome = fx.press(c.key);
        BOOST_TEST(outcome.handled);
        BOOST_TEST(!outcome.exit_requested);
        BOOST_TEST((fx.router.active_tab() == c.expected));
    }
}

// ---------------------------------------------------------------------------
// BUG-2 reset path: a flag an off-screen screen left set is cleared on the next
// tab entry, so navigation is never dead-locked even if some screen leaks.
// On failure first check: sync_active_screen clearing text_input_active before
// on_enter.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(tab_entry_clears_a_leaked_text_input_flag)
{
    AppNavFixture fx;

    // Simulate an off-screen leak, then enter the active tab from scratch.
    fx.context.text_input_active = true;
    fx.entered_tab.reset();
    tui::sync_active_screen(fx.screens, fx.router, fx.context, fx.entered_tab);
    BOOST_TEST(!fx.context.text_input_active);

    const tui::GlobalKeyOutcome outcome = fx.press('3');
    BOOST_TEST(outcome.handled);
    BOOST_TEST((fx.router.active_tab() == tui::ScreenId::tables));
}

// ---------------------------------------------------------------------------
// BUG-2 S4: once a screen whose default focus is a text Input is active, typing
// a digit must NOT navigate - it falls through to the focused Input. The
// Import/Export paste area is the canonical case.
// On failure first check: ImportExportComponent::update_text_input_active and
// the text-input guard in handle_global_key.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(digit_in_a_focused_input_does_not_navigate)
{
    AppNavFixture fx;
    tui::sync_active_screen(fx.screens, fx.router, fx.context, fx.entered_tab);

    // Switch to Import/Export and let its on_enter assert text focus.
    BOOST_TEST(fx.press('4').handled);
    tui::sync_active_screen(fx.screens, fx.router, fx.context, fx.entered_tab);
    BOOST_TEST((fx.router.active_tab() == tui::ScreenId::import_export));
    BOOST_TEST(fx.context.text_input_active);  // default focus = paste area

    const tui::ScreenId before = fx.router.active_tab();
    const tui::GlobalKeyOutcome outcome = fx.press('2');
    BOOST_TEST(!outcome.handled);  // falls through to the Input
    BOOST_TEST(!outcome.exit_requested);
    BOOST_TEST((fx.router.active_tab() == before));  // did not navigate
}

// ---------------------------------------------------------------------------
// BUG-1 (handler contract): Ctrl-C is reported as a clean exit request so
// App::run calls ScreenInteractive::Exit() and returns 0. The accompanying
// ForceHandleCtrlC(false) call in App::run (which suppresses FTXUI's SIGINT
// re-raise) needs a real terminal and is covered by the manual smoke checklist.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(ctrl_c_requests_a_clean_exit)
{
    AppNavFixture fx;
    tui::sync_active_screen(fx.screens, fx.router, fx.context, fx.entered_tab);

    bool help_visible = false;
    const tui::GlobalKeyOutcome outcome = tui::handle_global_key(
        ftxui::Event::CtrlC, fx.router, fx.context, help_visible);
    BOOST_TEST(outcome.handled);
    BOOST_TEST(outcome.exit_requested);
}

// ---------------------------------------------------------------------------
// BUG-1 (06.0/20 review): Ctrl-C must exit cleanly even while a text Input (or
// a typed-slug confirm) has focus. It is handled ABOVE the text-input guard, so
// it still reports `handled` + an exit request; if it fell through unhandled,
// FTXUI's `!handled` path would latch SIGINT and the process would exit 130,
// not 0 (D1/S1). Navigation keys typed into the Input still fall through.
// On failure first check: the Ctrl-C branch ordering in handle_global_key
// (must precede the text_input_active guard, like F1).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(ctrl_c_exits_cleanly_while_text_input_focused)
{
    AppNavFixture fx;
    tui::sync_active_screen(fx.screens, fx.router, fx.context, fx.entered_tab);

    // Enter Import/Export so a text field (the paste area) holds focus.
    BOOST_TEST(fx.press('4').handled);
    tui::sync_active_screen(fx.screens, fx.router, fx.context, fx.entered_tab);
    BOOST_TEST(fx.context.text_input_active);

    // Ctrl-C is still handled and requests a clean exit.
    bool help_visible = false;
    const tui::GlobalKeyOutcome ctrl_c = tui::handle_global_key(
        ftxui::Event::CtrlC, fx.router, fx.context, help_visible);
    BOOST_TEST(ctrl_c.handled);
    BOOST_TEST(ctrl_c.exit_requested);

    // The navigation keys still fall through to the focused Input (no nav, no
    // exit) while typing.
    const tui::ScreenId before = fx.router.active_tab();
    for (const char key : {'2', ':', '?'}) {
        const tui::GlobalKeyOutcome fall_through =
            tui::handle_global_key(ftxui::Event::Character(std::string(1, key)),
                                   fx.router,
                                   fx.context,
                                   help_visible);
        BOOST_TEST(!fall_through.handled);
        BOOST_TEST(!fall_through.exit_requested);
    }
    BOOST_TEST((fx.router.active_tab() == before));
    BOOST_TEST(!help_visible);  // `?` did not open help while typing
}

// ---------------------------------------------------------------------------
// D11 / spec S21: `q` at a non-Dashboard tab root returns to the Dashboard
// (edge `Settings --> Dashboard: q`). Before the fix Router::pop was a no-op at
// a tab root, so `q` did nothing.
// On failure first check: the `q` branch of handle_global_key.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(q_at_non_dashboard_tab_root_returns_to_dashboard)
{
    AppNavFixture fx;
    tui::sync_active_screen(fx.screens, fx.router, fx.context, fx.entered_tab);

    BOOST_TEST(fx.press('7').handled);  // Settings tab root
    BOOST_TEST((fx.router.active_tab() == tui::ScreenId::settings));

    const tui::GlobalKeyOutcome outcome = fx.press('q');
    BOOST_TEST(outcome.handled);
    BOOST_TEST(!outcome.exit_requested);
    BOOST_TEST((fx.router.active_tab() == tui::ScreenId::dashboard));
}

// ---------------------------------------------------------------------------
// D11: `q` on a nested route pops (unchanged). Switch to Links, push the nested
// Link detail, then `q` pops back to the Links root - not the Dashboard.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(q_on_a_nested_route_pops)
{
    AppNavFixture fx;
    tui::sync_active_screen(fx.screens, fx.router, fx.context, fx.entered_tab);

    BOOST_TEST(fx.press('2').handled);  // Links tab root
    BOOST_TEST(fx.router.push(tui::ScreenId::link_detail));  // nested route
    BOOST_TEST((fx.router.current() == tui::ScreenId::link_detail));

    const tui::GlobalKeyOutcome outcome = fx.press('q');
    BOOST_TEST(outcome.handled);
    BOOST_TEST(!outcome.exit_requested);
    BOOST_TEST((fx.router.current() == tui::ScreenId::links));  // popped
    BOOST_TEST((fx.router.active_tab() == tui::ScreenId::links));
}

// ---------------------------------------------------------------------------
// D11: `q` at the Dashboard root is a no-op and never quits the app.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(q_at_dashboard_root_is_a_noop)
{
    AppNavFixture fx;
    tui::sync_active_screen(fx.screens, fx.router, fx.context, fx.entered_tab);
    BOOST_TEST((fx.router.active_tab() == tui::ScreenId::dashboard));

    const tui::GlobalKeyOutcome outcome = fx.press('q');
    BOOST_TEST(!outcome.handled);
    BOOST_TEST(!outcome.exit_requested);
    BOOST_TEST((fx.router.active_tab() == tui::ScreenId::dashboard));
}

// ---------------------------------------------------------------------------
// D11: `q` stays typable in a focused Input - it falls through (no back, no
// nav, no exit) so it reaches the field. Also confirms Esc is still a no-op
// back key at a tab root (unchanged by the `q` fix).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(q_in_a_focused_input_is_typed_and_esc_is_unchanged)
{
    AppNavFixture fx;
    tui::sync_active_screen(fx.screens, fx.router, fx.context, fx.entered_tab);

    // Focus a text field (Import/Export paste area).
    BOOST_TEST(fx.press('4').handled);
    tui::sync_active_screen(fx.screens, fx.router, fx.context, fx.entered_tab);
    BOOST_TEST(fx.context.text_input_active);

    const tui::ScreenId before = fx.router.active_tab();
    const tui::GlobalKeyOutcome typed = fx.press('q');
    BOOST_TEST(!typed.handled);  // falls through to the Input
    BOOST_TEST(!typed.exit_requested);
    BOOST_TEST((fx.router.active_tab() == before));

    // Esc behavior is unchanged by the `q` fix: unlike `q`, Esc is a plain
    // no-op at a non-Dashboard tab root (it does not return to the Dashboard).
    fx.context.text_input_active = false;  // leave the field so keys dispatch
    BOOST_TEST(fx.press('7').handled);  // Settings tab root
    BOOST_TEST((fx.router.active_tab() == tui::ScreenId::settings));
    bool help_visible = false;
    const tui::GlobalKeyOutcome esc = tui::handle_global_key(
        ftxui::Event::Escape, fx.router, fx.context, help_visible);
    BOOST_TEST(!esc.handled);  // pop() is a no-op at a tab root
    BOOST_TEST(
        (fx.router.active_tab() == tui::ScreenId::settings));  // unchanged
}
