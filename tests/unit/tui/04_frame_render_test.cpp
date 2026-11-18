#define BOOST_TEST_MODULE TuiFrameRenderTest

#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <ftxui/dom/elements.hpp>

#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/frame_model.hpp>
#include <url_shortener/tui/frame_view.hpp>
#include <url_shortener/tui/keymap.hpp>
#include <url_shortener/tui/screen_id.hpp>
#include <url_shortener/tui/screens/placeholder_screen.hpp>

#include "golden.hpp"
#include "render_helpers.hpp"

using url_shortener::tui::FrameModel;
using url_shortener::tui::KeyHint;
using url_shortener::tui::placeholder_body;
using url_shortener::tui::render_frame;
using url_shortener::tui::ScreenId;
using url_shortener::tui::SessionInfo;
using url_shortener::tui::testing::assert_matches_golden;
using url_shortener::tui::testing::render_to_text;

namespace
{

constexpr char kEsc = '\x1b';

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

}  // namespace

// ---------------------------------------------------------------------------
// The 80x24 frame with a persistent backend matches its golden.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(frame_80x24_matches_golden)
{
    FrameModel model(admin_session(), "sqlite (links.db)", true, "dark");
    const ftxui::Element frame = render_frame(
        model, ScreenId::dashboard, dashboard_body(), {}, 80, 24);
    const std::string actual = render_to_text(frame, 80, 24);

    BOOST_TEST(actual.find(kEsc) == std::string::npos);
    assert_matches_golden(actual, "d01_frame_80x24.txt");
}

// ---------------------------------------------------------------------------
// A non-persistent backend adds the memory-store banner row.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(frame_with_memory_banner_matches_golden)
{
    FrameModel model(admin_session(), "memory", false, "dark");
    const ftxui::Element frame = render_frame(
        model, ScreenId::dashboard, dashboard_body(), {}, 80, 24);
    const std::string actual = render_to_text(frame, 80, 24);

    BOOST_TEST(actual.find("memory store - data is lost on exit")
               != std::string::npos);
    assert_matches_golden(actual, "d01_frame_memory_banner_80x24.txt");
}

// ---------------------------------------------------------------------------
// Below 80x24 the frame is replaced by the too-small message.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(narrow_terminal_shows_too_small_message)
{
    FrameModel model(admin_session(), "sqlite", true, "dark");
    const ftxui::Element frame = render_frame(
        model, ScreenId::dashboard, dashboard_body(), {}, 60, 20);
    const std::string actual = render_to_text(frame, 60, 20);

    BOOST_TEST(actual.find("terminal too small (need 80x24)")
               != std::string::npos);
    // The tab bar / title must not render in the degraded view.
    BOOST_TEST(actual.find("[1]Dashboard") == std::string::npos);
    assert_matches_golden(actual, "d01_frame_narrow_60x20.txt");
}

// ---------------------------------------------------------------------------
// Resizing 80x24 -> 100x30 keeps the footer on the last content row.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(resize_keeps_footer_on_last_row)
{
    FrameModel model(admin_session(), "sqlite (links.db)", true, "dark");
    const ftxui::Element frame = render_frame(
        model, ScreenId::dashboard, dashboard_body(), {}, 100, 30);
    const std::string actual = render_to_text(frame, 100, 30);
    const std::vector<std::string> lines = split(actual);

    // Find the footer row (holds the global "Ctrl-C quit" hint).
    int footer_line = -1;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i)
    {
        if (lines[i].find("Ctrl-C quit") != std::string::npos)
        {
            footer_line = i;
        }
    }
    BOOST_REQUIRE(footer_line >= 0);
    // The footer sits just above the window's bottom border (the last row).
    BOOST_TEST(footer_line >= static_cast<int>(lines.size()) - 2);
}
