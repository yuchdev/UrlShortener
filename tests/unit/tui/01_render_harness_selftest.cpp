#define BOOST_TEST_MODULE TuiRenderHarnessSelfTest

#include <algorithm>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "url_shortener/tui/ftxui_smoke.hpp"

#include "golden.hpp"
#include "render_helpers.hpp"

using url_shortener::tui::render_smoke_element;
using url_shortener::tui::testing::assert_matches_golden;
using url_shortener::tui::testing::render_to_text;
using url_shortener::tui::testing::text_diff;
using url_shortener::tui::testing::update_golden_enabled;

namespace
{

/// 0x1B (ESC) introduces every SGR/CSI/OSC sequence; its absence is the
/// invariant that the harness reads only cell characters, never styled output.
constexpr char kEsc = '\x1b';

}  // namespace

// ---------------------------------------------------------------------------
// Smoke window matches its golden (proves FTXUI links + the render pipeline).
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(smoke_window_matches_golden)
{
    const std::string actual = render_to_text(render_smoke_element(), 20, 3);

    // Must be pure text: no escape bytes may reach the grid.
    BOOST_TEST(actual.find(kEsc) == std::string::npos);
    // Three fixed rows, newline-joined (top border, body, bottom border).
    BOOST_TEST(std::count(actual.begin(), actual.end(), '\n') == 2);

    assert_matches_golden(actual, "00_smoke_20x3.txt");
}

// ---------------------------------------------------------------------------
// Colour/style is stripped: a red "x" renders as a plain "x" with no ESC.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(colour_is_stripped_from_rendered_text)
{
    const ftxui::Element coloured =
        ftxui::text("x") | ftxui::color(ftxui::Color::Red);
    const std::string actual = render_to_text(coloured, 3, 1);

    BOOST_TEST(actual.find(kEsc) == std::string::npos);
    BOOST_TEST(actual == "x");
}

// ---------------------------------------------------------------------------
// Degenerate sizes must not crash and must stay ESC-free.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(zero_and_tiny_dimensions_do_not_crash)
{
    const ftxui::Element element = render_smoke_element();

    const std::string zero = render_to_text(element, 0, 0);
    BOOST_TEST(zero.empty());
    BOOST_TEST(zero.find(kEsc) == std::string::npos);

    const std::string tiny = render_to_text(element, 1, 1);
    BOOST_TEST(tiny.find(kEsc) == std::string::npos);
    // 1x1 is a single row with no newline.
    BOOST_TEST(tiny.find('\n') == std::string::npos);

    // Negative dimensions clamp to 0 rather than throwing.
    const std::string negative = render_to_text(element, -5, -5);
    BOOST_TEST(negative.empty());
}

// ---------------------------------------------------------------------------
// The mismatch reporter returns a readable, line-numbered diff.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(text_diff_reports_a_readable_difference)
{
    // Identical inputs produce no diff.
    BOOST_TEST(text_diff("same\nlines", "same\nlines").empty());

    // Differing inputs name the line and show both sides.
    const std::string diff = text_diff("alpha\nbeta", "alpha\nGAMMA");
    BOOST_TEST(!diff.empty());
    BOOST_TEST(diff.find("line 2") != std::string::npos);
    BOOST_TEST(diff.find("- beta") != std::string::npos);
    BOOST_TEST(diff.find("+ GAMMA") != std::string::npos);
    // The matching first line is not reported.
    BOOST_TEST(diff.find("alpha") == std::string::npos);
}

// ---------------------------------------------------------------------------
// UPDATE_GOLDEN is opt-in: a normal CTest run never rewrites goldens.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(update_golden_is_off_by_default)
{
    BOOST_TEST(update_golden_enabled() == false);
}
