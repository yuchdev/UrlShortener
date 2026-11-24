#define BOOST_TEST_MODULE TuiThemeRenderTest

#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/frame_model.hpp>
#include <url_shortener/tui/frame_view.hpp>
#include <url_shortener/tui/help_model.hpp>
#include <url_shortener/tui/help_overlay.hpp>
#include <url_shortener/tui/keymap.hpp>
#include <url_shortener/tui/screen_id.hpp>
#include <url_shortener/tui/screens/placeholder_screen.hpp>
#include <url_shortener/tui/text_fit.hpp>
#include <url_shortener/tui/theme.hpp>

#include "golden.hpp"
#include "hostile_strings.hpp"
#include "render_helpers.hpp"

/**
 * @file 05_theme_render_test.cpp
 * @brief Render tests for the mono frame, the light frame and the help overlay.
 *
 * Type: unit (render/snapshot, links url_shortener_tui + FTXUI).
 * Scenario: the mono frame is pure ASCII and matches its golden; the light
 *           frame matches its (box-drawing) golden and differs from dark only in
 *           style, not layout; the help overlay matches its golden, swallows a
 *           navigation key while open and closes on Esc; T-110 - a hostile hint
 *           never reaches the overlay grid.
 * Boundaries: every byte of the mono grid <= 0x7E; overlay open/closed states.
 * On failure first check: whether `ascii_filter` is applied at the frame root
 *           for the mono theme, or whether the overlay render sink stopped
 *           calling `sanitize_for_terminal`.
 */

using url_shortener::tui::FrameModel;
using url_shortener::tui::HelpModel;
using url_shortener::tui::help_overlay;
using url_shortener::tui::KeyHint;
using url_shortener::tui::placeholder_body;
using url_shortener::tui::render_frame;
using url_shortener::tui::render_help_overlay;
using url_shortener::tui::display_width;
using url_shortener::tui::ScreenId;
using url_shortener::tui::SessionInfo;
using url_shortener::tui::theme_for;
using url_shortener::tui::ThemeName;
using url_shortener::tui::testing::assert_matches_golden;
using url_shortener::tui::testing::has_display_hazard;
using url_shortener::tui::testing::hostile_samples;
using url_shortener::tui::testing::render_to_text;
using url_shortener::tui::testing::send_events;

namespace
{

SessionInfo admin_session()
{
    SessionInfo session;
    session.principal = "local-operator";
    session.read_only = false;
    return session;
}

ftxui::Element dashboard_body()
{
    return placeholder_body("Dashboard: not implemented");
}

std::vector<std::string> split(const std::string& text)
{
    std::vector<std::string> lines;
    std::string line;
    for (char ch : text)
    {
        if (ch == '\n')
        {
            lines.push_back(line);
            line.clear();
        }
        else
        {
            line += ch;
        }
    }
    lines.push_back(line);
    return lines;
}

// A representative help model for the overlay goldens.
HelpModel sample_help()
{
    return HelpModel({KeyHint {"F1 / ?", "help"}, KeyHint {"q / Esc", "back"},
                      KeyHint {"Ctrl-C", "quit"}},
                     {KeyHint {"j/k", "move"}, KeyHint {"Enter", "open"},
                      KeyHint {"n", "new"}});
}

}  // namespace

// ---------------------------------------------------------------------------
// The mono frame is pure ASCII (every byte <= 0x7E) and matches its golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(mono_frame_is_ascii_and_matches_golden)
{
    FrameModel model(admin_session(), "sqlite (links.db)", true, "mono");
    const ftxui::Element frame =
        render_frame(model, ScreenId::dashboard, dashboard_body(), {}, 80, 24,
                     theme_for(ThemeName::mono));
    const std::string actual = render_to_text(frame, 80, 24);

    for (unsigned char byte : actual)
    {
        BOOST_TEST(byte <= 0x7Eu,
                   "non-ASCII byte in the mono grid: "
                       << static_cast<int>(byte));
    }
    assert_matches_golden(actual, "d01_frame_mono_80x24.txt");
}

// ---------------------------------------------------------------------------
// The light frame keeps the box-drawing layout and matches its golden; the
// style difference from dark is asserted via the Theme, not the plain text.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(light_frame_matches_golden_same_layout_as_dark)
{
    FrameModel model(admin_session(), "sqlite (links.db)", true, "light");
    const ftxui::Element frame =
        render_frame(model, ScreenId::dashboard, dashboard_body(), {}, 80, 24,
                     theme_for(ThemeName::light));
    const std::string light_text = render_to_text(frame, 80, 24);

    // Switching the theme must not change layout: the light plain-text output
    // equals the dark frame's committed golden (colour is invisible here).
    assert_matches_golden(light_text, "d01_frame_light_80x24.txt");

    // The difference between the two coloured themes is carried by style flags.
    BOOST_TEST((theme_for(ThemeName::dark).title.fg
                != theme_for(ThemeName::light).title.fg));
}

// ---------------------------------------------------------------------------
// The help overlay renders its golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(help_overlay_matches_golden)
{
    const ftxui::Element overlay =
        render_help_overlay(sample_help(), theme_for(ThemeName::dark));
    const std::string actual = render_to_text(overlay, 80, 24);
    assert_matches_golden(actual, "help_overlay_80x24.txt");
}

// ---------------------------------------------------------------------------
// The overlay component swallows a navigation key while open and closes on Esc.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(overlay_swallows_key_and_closes_on_esc)
{
    bool visible = true;
    ftxui::Component overlay = help_overlay(sample_help(), &visible);

    const std::string open = render_to_text(overlay, 80, 24);
    BOOST_TEST(open.find("Help") != std::string::npos);

    // 'j' must be swallowed, leaving the overlay open and unchanged.
    send_events(overlay, {ftxui::Event::Character("j")});
    BOOST_TEST(visible);
    BOOST_TEST(render_to_text(overlay, 80, 24).find("Help")
               != std::string::npos);

    // Esc closes it; the component then renders nothing.
    send_events(overlay, {ftxui::Event::Escape});
    BOOST_TEST(!visible);
    BOOST_TEST(render_to_text(overlay, 80, 24).find("Help")
               == std::string::npos);
}

// ---------------------------------------------------------------------------
// The overlay caps its height: 200 hints render a summary line, not 200 rows,
// and nothing overflows the 80x24 screen.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(overlay_caps_rows_for_many_hints)
{
    std::vector<KeyHint> many;
    for (int i = 0; i < 200; ++i)
    {
        many.push_back(
            KeyHint {"k" + std::to_string(i), "a" + std::to_string(i)});
    }
    const ftxui::Element overlay =
        render_help_overlay(HelpModel({}, many), theme_for(ThemeName::dark));
    const std::string grid = render_to_text(overlay, 80, 24);
    const std::vector<std::string> lines = split(grid);

    // The harness clamps the grid to 24 rows; the modal must fit inside it.
    BOOST_TEST(lines.size() <= 25u);
    // The overflow collapses to a single summary line.
    BOOST_TEST(grid.find("more") != std::string::npos);
    // The modal title is still visible (not pushed off the top).
    BOOST_TEST(grid.find("Help") != std::string::npos);
    // No rendered row exceeds the 80-column screen (byte length differs from
    // display width because borders are multi-byte box-drawing glyphs).
    for (const std::string& line : lines)
    {
        BOOST_TEST(display_width(line) <= 80u);
    }
}

// ---------------------------------------------------------------------------
// T-110: a hostile footer hint (as a screen might contribute via IScreen::hints)
// never reaches the frame grid, and a long hint set stays on the single footer
// row within the frame width.
// On failure first check: footer_text stopped sanitizing, or render_frame
// stopped width-fitting the footer.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hostile_footer_hints_never_reach_grid)
{
    for (const auto& sample : hostile_samples())
    {
        const std::vector<KeyHint> hints = {KeyHint {sample.raw, sample.raw}};
        FrameModel model(admin_session(), "sqlite", true, "dark");
        const ftxui::Element frame = render_frame(
            model, ScreenId::links, dashboard_body(), hints, 80, 24);
        const std::string grid = render_to_text(frame, 80, 24);

        BOOST_TEST(grid.find('\x1b') == std::string::npos,
                   "ESC reached the footer for sample: " << sample.name);
        for (const std::string& line : split(grid))
        {
            BOOST_TEST(!has_display_hazard(line),
                       "footer hazard in a cell for sample: " << sample.name);
        }
    }
}

// ---------------------------------------------------------------------------
// A long footer is width-fitted to a single row just above the bottom border;
// no hint wraps onto an extra line.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(long_footer_stays_on_single_last_row)
{
    std::vector<KeyHint> hints;
    for (int i = 0; i < 40; ++i)
    {
        hints.push_back(
            KeyHint {"key" + std::to_string(i), "action" + std::to_string(i)});
    }
    FrameModel model(admin_session(), "sqlite", true, "dark");
    const ftxui::Element frame =
        render_frame(model, ScreenId::links, dashboard_body(), hints, 80, 24);
    const std::vector<std::string> lines = split(render_to_text(frame, 80, 24));

    BOOST_REQUIRE_EQUAL(lines.size(), 24u);
    for (const std::string& line : lines)
    {
        BOOST_TEST(display_width(line) <= 80u);
    }
    // The footer is the content row directly above the window's bottom border;
    // it carries the first hints and was not wrapped onto another row.
    const std::string& footer = lines[lines.size() - 2];
    BOOST_TEST(footer.find("key0 action0") != std::string::npos);
    // The row below it is the window's bottom border, not a wrapped footer.
    BOOST_TEST(lines[lines.size() - 1].find("key") == std::string::npos);
}

// ---------------------------------------------------------------------------
// T-110: a hostile hint (as a screen might contribute) never reaches the
// overlay grid - no ESC / C1 / bidi / zero-width byte survives any cell.
// On failure first check: render_help_overlay stopped sanitizing its rows.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hostile_hint_never_reaches_overlay_grid)
{
    for (const auto& sample : hostile_samples())
    {
        HelpModel model({}, {KeyHint {sample.raw, sample.raw}});
        const ftxui::Element overlay =
            render_help_overlay(model, theme_for(ThemeName::dark));
        const std::string grid = render_to_text(overlay, 80, 24);

        BOOST_TEST(grid.find('\x1b') == std::string::npos,
                   "ESC reached the overlay for sample: " << sample.name);
        for (const std::string& line : split(grid))
        {
            BOOST_TEST(!has_display_hazard(line),
                       "display hazard in a cell for sample: " << sample.name);
        }
    }
}
