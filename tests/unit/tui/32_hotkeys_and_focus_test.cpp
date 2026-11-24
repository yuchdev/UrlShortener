#define BOOST_TEST_MODULE TuiHotkeysAndFocusTest

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <ftxui/component/event.hpp>
#include <url_shortener/tui/app_navigation.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/capability.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/help_model.hpp>
#include <url_shortener/tui/keymap.hpp>
#include <url_shortener/tui/palette_model.hpp>
#include <url_shortener/tui/router.hpp>
#include <url_shortener/tui/screen.hpp>
#include <url_shortener/tui/screen_id.hpp>
#include <url_shortener/tui/screens/dashboard_screen.hpp>
#include <url_shortener/tui/screens/import_export_screen.hpp>
#include <url_shortener/tui/screens/jobs_screen.hpp>
#include <url_shortener/tui/screens/links_screen.hpp>
#include <url_shortener/tui/screens/palette_screen.hpp>
#include <url_shortener/tui/screens/settings_screen.hpp>
#include <url_shortener/tui/screens/tables_screen.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/tui_options.hpp>

#include "inline_poster.hpp"
#include "mock_command_client.hpp"

/**
 * @file 32_hotkeys_and_focus_test.cpp
 * @brief Hotkey, focus-rule and palette completeness for the TUI (07.0/27, gap
 *        `tui__32`).
 *
 * Type: unit (the public hotkey/help/palette model objects and the real screen
 * `hints()` footers, driven headlessly; links url_shortener_tui + FTXUI).
 *
 * Scenario: the hotkey story is asserted against the source-of-truth objects,
 * never scraped rendered text (spec step 2). The global footer keys
 * (@ref global_hints) that `App::run` advertises must actually be dispatched by
 * the one global hotkey handler @ref handle_global_key, so the footer never
 * promises a key the app ignores; the F1 help overlay's model (@ref HelpModel,
 * whose mechanics `tui__05_help_model_test` owns) must carry every global key
 * and every screen hint without dropping one; within a single screen no key is
 * bound to two different meanings; and the Command Palette (@ref PaletteModel,
 * D9) must list every catalog command so every action is reachable with no
 * bespoke code (ADR 0003 Decision 2 rule 2).
 *
 * Boundaries: a back key that acts (nested route / non-Dashboard root) vs one
 * that is a documented no-op at the Dashboard root; the global keymap vs a
 * screen's own hints in the help overlay.
 *
 * On failure first check: a footer hint added without wiring the key in
 * `src/tui/app_navigation.cpp`; a screen's `hints()` with a duplicate key; or a
 * catalog command the palette model fails to surface
 * (`src/tui/palette_model.cpp`).
 */

namespace tui = url_shortener::tui;
namespace commands = url_shortener::commands;

using tui::ScreenId;
using tui::testing::MockCommandClient;

namespace
{

/// A representative catalog spanning read/write and job commands, so the
/// palette completeness check exercises every tag path.
std::vector<tui::CommandInfo> catalog()
{
    const auto make = [](std::string name, std::string permission, bool job)
    {
        tui::CommandInfo info;
        info.name = std::move(name);
        info.summary = info.name + " summary";
        info.permission = std::move(permission);
        info.is_job = job;
        info.mutating = info.permission != "read";
        return info;
    };
    return {
        make("system.info", "read", false),
        make("stats.summary", "read", false),
        make("link.list", "read", false),
        make("link.get", "read", false),
        make("link.stats", "read", false),
        make("link.create", "write", false),
        make("link.update", "write", false),
        make("link.delete", "write", false),
        make("link.import", "write", true),
        make("link.export", "read", true),
        make("db.tables", "read", false),
        make("db.rows", "read", false),
        make("job.list", "read", false),
        make("job.get", "read", false),
        make("job.cancel", "write", false),
    };
}

/**
 * @brief Builds every real tab screen over one shared @ref ScreenContext, the
 * way `tui_entry`/`App::run` build them, so each screen's `hints()` footer is
 * the real one the frame renders.
 */
struct ScreensFixture
{
    MockCommandClient client;
    tui::testing::InlinePoster poster;
    tui::AsyncCaller caller {client, poster, 1};
    tui::Router router;
    tui::SessionInfo session;
    tui::TuiOptions options {make_options()};
    tui::ScreenContext context {caller, session, router, options};
    std::vector<std::unique_ptr<tui::IScreen>> screens;

    ScreensFixture()
    {
        client.set_commands(catalog());
        const std::vector<tui::CommandInfo> cat = catalog();
        screens.push_back(tui::make_dashboard_screen(context, cat));
        screens.push_back(tui::make_settings_screen(context));
        screens.push_back(tui::make_links_screen(context, cat));
        screens.push_back(tui::make_tables_screen(context, cat));
        screens.push_back(tui::make_palette_screen(context, cat));
        screens.push_back(tui::make_jobs_screen(context, cat));
        screens.push_back(tui::make_import_export_screen(context, cat));
    }

    static tui::TuiOptions make_options()
    {
        tui::TuiOptions o;
        o.theme = tui::ThemeName::dark;
        o.read_only = false;
        return o;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Every global key the footer advertises (`global_hints`: back = q/Esc,
// quit = Ctrl-C) plus the tab/palette/help keys the help overlay documents are
// actually dispatched by `handle_global_key`, so the footer/overlay never
// promises a key the app ignores. Each key is tested in a state where it is
// meant to act (the back keys are a documented no-op at the Dashboard root).
// On failure first check: a key branch removed from handle_global_key in
// src/tui/app_navigation.cpp while its footer hint remained.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(advertised_global_keys_are_dispatched)
{
    MockCommandClient client;
    tui::testing::InlinePoster poster;
    tui::AsyncCaller caller {client, poster, 1};
    tui::SessionInfo session;
    tui::TuiOptions options;

    // The footer names both back keys as "q/Esc"; prove each is wired.
    {
        tui::Router router;
        tui::ScreenContext context {caller, session, router, options};
        bool help = false;
        router.navigate_tab('2');  // Links tab root
        BOOST_TEST(router.push(ScreenId::link_detail));  // a nested route
        // Esc pops the nested route (handled).
        const tui::GlobalKeyOutcome esc =
            tui::handle_global_key(ftxui::Event::Escape, router, context, help);
        BOOST_TEST(esc.handled);
        BOOST_TEST((router.current() == ScreenId::links));
    }
    {
        tui::Router router;
        tui::ScreenContext context {caller, session, router, options};
        bool help = false;
        router.navigate_tab('7');  // a non-Dashboard tab root
        // `q` returns from a non-Dashboard root to the Dashboard (handled).
        const tui::GlobalKeyOutcome q = tui::handle_global_key(
            ftxui::Event::Character("q"), router, context, help);
        BOOST_TEST(q.handled);
        BOOST_TEST((router.active_tab() == ScreenId::dashboard));
    }

    // Ctrl-C is the quit key, dispatched (and reported as an exit request) from
    // anywhere.
    {
        tui::Router router;
        tui::ScreenContext context {caller, session, router, options};
        bool help = false;
        const tui::GlobalKeyOutcome quit =
            tui::handle_global_key(ftxui::Event::CtrlC, router, context, help);
        BOOST_TEST(quit.handled);
        BOOST_TEST(quit.exit_requested);
    }

    // F1 / `?` open help; the seven digit tabs and `:` switch tab - every key
    // the help overlay documents is wired.
    {
        tui::Router router;
        tui::ScreenContext context {caller, session, router, options};
        bool help = false;
        BOOST_TEST(
            tui::handle_global_key(ftxui::Event::F1, router, context, help)
                .handled);
        BOOST_TEST(help);
        help = false;
        BOOST_TEST(tui::handle_global_key(
                       ftxui::Event::Character("?"), router, context, help)
                       .handled);
        BOOST_TEST(help);
    }
    for (const char key : {'1', '2', '3', '4', '5', '6', '7', ':'}) {
        tui::Router router;
        tui::ScreenContext context {caller, session, router, options};
        bool help = false;
        const tui::GlobalKeyOutcome outcome =
            tui::handle_global_key(ftxui::Event::Character(std::string(1, key)),
                                   router,
                                   context,
                                   help);
        BOOST_TEST(outcome.handled, "tab key '" << key << "' not dispatched");
    }
}

// ---------------------------------------------------------------------------
// Within any single screen's footer no key is bound to two different meanings
// (a key may repeat only with the identical label). A collision would let one
// keystroke mean two things on the same screen.
// On failure first check: the `hints()` list of the named screen.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(no_intra_screen_hotkey_binds_two_meanings)
{
    ScreensFixture fx;
    for (const std::unique_ptr<tui::IScreen>& screen : fx.screens) {
        std::map<std::string, std::string> key_to_label;
        for (const tui::KeyHint& hint : screen->hints()) {
            const auto it = key_to_label.find(hint.keys);
            if (it == key_to_label.end()) {
                key_to_label.emplace(hint.keys, hint.label);
            }
            else {
                BOOST_TEST(it->second == hint.label,
                           "screen " << static_cast<int>(screen->id())
                                     << " binds key '" << hint.keys
                                     << "' to both '" << it->second << "' and '"
                                     << hint.label << "'");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// The F1 help overlay model carries every global footer key and every screen
// hint, in order, without dropping a distinct binding: a screen that re-lists a
// global key appears once, but every unique key/label survives. This ties the
// footers to the overlay through the same HelpModel the overlay renders
// (`tui__05_help_model_test` owns the dedup mechanics).
// On failure first check: HelpModel::entries in src/tui/help_model.cpp.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(help_overlay_lists_globals_and_screen_hints)
{
    ScreensFixture fx;
    const std::vector<tui::KeyHint> global = tui::global_hints();

    for (const std::unique_ptr<tui::IScreen>& screen : fx.screens) {
        const std::vector<tui::KeyHint> screen_hints = screen->hints();
        const tui::HelpModel model(global, screen_hints);
        const std::vector<tui::HelpEntry> entries = model.entries();

        // Every global footer key is present in the overlay.
        for (const tui::KeyHint& g : global) {
            bool found = false;
            for (const tui::HelpEntry& e : entries) {
                found = found || (e.keys == g.keys && e.label == g.label);
            }
            BOOST_TEST(
                found,
                "global key '" << g.keys << "' missing from the overlay");
        }
        // Every screen hint is present in the overlay.
        for (const tui::KeyHint& s : screen_hints) {
            bool found = false;
            for (const tui::HelpEntry& e : entries) {
                found = found || (e.keys == s.keys && e.label == s.label);
            }
            BOOST_TEST(found,
                       "screen hint '" << s.keys << "' ("
                                       << static_cast<int>(screen->id())
                                       << ") missing from the overlay");
        }
    }
}

// ---------------------------------------------------------------------------
// The Command Palette lists every catalog command (D9: every registry command
// reachable with zero bespoke TUI code), so every action a hotkey reaches is
// also reachable through the palette. An empty query lists all commands exactly
// once.
// On failure first check: PaletteModel::rebuild/entries in palette_model.cpp.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(palette_lists_every_catalog_command)
{
    const std::vector<tui::CommandInfo> cat = catalog();
    tui::SessionInfo session;
    session.read_only = false;
    tui::PaletteModel model(cat, tui::CapabilityModel(session));

    const std::vector<tui::PaletteEntry>& entries = model.entries();
    BOOST_REQUIRE_EQUAL(entries.size(), cat.size());

    for (const tui::CommandInfo& command : cat) {
        std::size_t seen = 0;
        for (const tui::PaletteEntry& entry : entries) {
            if (entry.info != nullptr && entry.info->name == command.name) {
                ++seen;
            }
        }
        BOOST_TEST(seen == 1u,
                   "catalog command '" << command.name << "' appears " << seen
                                       << " times in the palette");
    }
}
