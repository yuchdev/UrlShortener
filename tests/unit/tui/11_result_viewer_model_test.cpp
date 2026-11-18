#define BOOST_TEST_MODULE TuiResultViewerModelTest

#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/tui/result_viewer_model.hpp>
#include <url_shortener/tui/text_fit.hpp>

#include "hostile_strings.hpp"

/**
 * @file 11_result_viewer_model_test.cpp
 * @brief Tests for the Command Palette result viewer model (D9, 06.0/11).
 *
 * Type: unit (pure model, links url_shortener_tui_model, no FTXUI).
 * Scenario: long values wrap at the target width; scrolling clamps to the
 *           content; empty/`{}` input is handled; a 10,000-line result stays
 *           bounded and lazily windowed (T-121); a deep/wide 8 MiB result keeps
 *           both memory and per-frame cost bounded (T-121); a hostile result
 *           string (ESC/OSC/bidi) yields visible lines with no display hazard
 *           (T-110).
 * Boundaries: empty vs one line vs 10,000 lines vs 8 MiB; width wrapping;
 *           pretty-printed (small) vs compact (over-cap) input.
 * On failure first check: build()/wrap_into_lines() bounds in
 *           result_viewer_model.cpp and that visible_lines re-sanitizes.
 */

namespace tui = url_shortener::tui;

using tui::testing::has_display_hazard;
using tui::testing::hostile_samples;

// ---------------------------------------------------------------------------
// A long value wraps so every visible line fits the target width.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(wraps_long_value_at_width)
{
    const std::string json = "\"" + std::string(200, 'a') + "\"";
    tui::ResultViewerModel model(json, 20);
    BOOST_TEST(model.total_lines() > 1u);
    for (const std::string& line : model.visible_lines(100)) {
        BOOST_TEST(tui::display_width(line) <= 20u);
    }
}

// ---------------------------------------------------------------------------
// Scrolling clamps to the content on both ends.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(scroll_clamps)
{
    std::string json = "[";
    for (int i = 0; i < 50; ++i) {
        json += (i == 0 ? "" : ",");
        json += std::to_string(i);
    }
    json += "]";

    tui::ResultViewerModel model(json, 40);
    BOOST_REQUIRE(model.total_lines() > 10u);

    model.scroll(-100);
    BOOST_TEST(model.offset() == 0u);

    model.scroll(1000000);
    BOOST_TEST(model.offset() == model.total_lines() - 1u);

    model.page(-1000);
    BOOST_TEST(model.offset() == 0u);
}

// ---------------------------------------------------------------------------
// Empty and `{}` inputs are handled without crashing.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(empty_and_trivial_input)
{
    tui::ResultViewerModel empty("", 40);
    BOOST_TEST(empty.total_lines() == 0u);
    BOOST_TEST(empty.visible_lines(10).empty());

    tui::ResultViewerModel obj("{}", 40);
    BOOST_REQUIRE_EQUAL(obj.total_lines(), 1u);
    BOOST_TEST(obj.visible_lines(10).front() == "{}");
}

// ---------------------------------------------------------------------------
// A 10,000-line result is retained in full but windowed lazily: visible_lines
// returns only the requested height, and the per-frame cost is O(height).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(ten_thousand_lines_handled_lazily)
{
    std::string json = "[";
    for (int i = 0; i < 10000; ++i) {
        json += (i == 0 ? "" : ",");
        json += std::to_string(i);
    }
    json += "]";

    tui::ResultViewerModel model(json, 80);
    BOOST_TEST(model.total_lines() >= 10000u);
    BOOST_TEST(model.total_lines()
               <= tui::ResultViewerModel::k_max_display_lines);
    BOOST_TEST(model.visible_lines(24).size() == 24u);
}

// ---------------------------------------------------------------------------
// T-121: a deep + wide 8 MiB result stays bounded - it is not pretty-amplified,
// the materialized line count is capped, and visible_lines is O(height).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(eight_mib_result_stays_bounded)
{
    // A deep then wide payload: 16 nested arrays around a wide array of numbers,
    // padded past 8 MiB. Pretty-printing this would amplify to gigabytes.
    std::string wide = "[";
    while (wide.size() < 8u * 1024u * 1024u) {
        wide += "1,";
    }
    wide += "1]";
    std::string json;
    for (int i = 0; i < 16; ++i) {
        json += "[";
    }
    json += wide;
    for (int i = 0; i < 16; ++i) {
        json += "]";
    }

    tui::ResultViewerModel model(json, 80);
    // Bounded line count (memory) and bounded window cost (per frame).
    BOOST_TEST(model.total_lines()
               <= tui::ResultViewerModel::k_max_display_lines);
    BOOST_TEST(model.visible_lines(24).size() <= 24u);
    for (const std::string& line : model.visible_lines(24)) {
        BOOST_TEST(tui::display_width(line) <= 80u);
    }
}

// ---------------------------------------------------------------------------
// T-110: a hostile result string yields visible lines with no control/bidi/
// zero-width byte, whether fed raw or inside an object.
// On failure first check: visible_lines stopped sanitizing, or build() wrapped
// before sanitizing.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hostile_result_never_leaks_hazard)
{
    for (const auto& sample : hostile_samples()) {
        const std::string wrapped =
            std::string("{\"v\":\"") + sample.raw + "\"}";
        for (const std::string& json : {sample.raw, wrapped}) {
            tui::ResultViewerModel model(json, 40);
            for (const std::string& line : model.visible_lines(200)) {
                BOOST_TEST(!has_display_hazard(line),
                           "display hazard for sample: " << sample.name);
            }
        }
    }
}
