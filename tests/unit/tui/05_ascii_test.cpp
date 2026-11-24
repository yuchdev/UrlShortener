#define BOOST_TEST_MODULE TuiAsciiTest

#include <string>

#include <boost/test/unit_test.hpp>

#include <url_shortener/tui/ascii.hpp>

/**
 * @file 05_ascii_test.cpp
 * @brief Unit tests for the mono-theme box/block to ASCII transliteration.
 *
 * Type: unit (model library, FTXUI-free).
 * Scenario: light/heavy/double/rounded box glyphs and block/shade glyphs map to
 *           `+ - | #`; ordinary and multibyte text is untouched; invalid and
 *           truncated UTF-8 pass through; the mapping is idempotent.
 * Boundaries: a code point exactly on the box-drawing block edges, a truncated
 *             trailing sequence, and double application.
 * On failure first check: the `box_ascii` classification in `src/tui/ascii.cpp`.
 */

using url_shortener::tui::transliterate_box_chars;

namespace
{

// A few representative box/block glyphs encoded as UTF-8 literals.
const std::string k_light_horizontal = "\xe2\x94\x80";  // U+2500 ─
const std::string k_heavy_horizontal = "\xe2\x94\x81";  // U+2501 ━
const std::string k_double_horizontal = "\xe2\x95\x90"; // U+2550 ═
const std::string k_light_vertical = "\xe2\x94\x82";    // U+2502 │
const std::string k_heavy_vertical = "\xe2\x94\x83";    // U+2503 ┃
const std::string k_corner_tl = "\xe2\x94\x8c";         // U+250C ┌
const std::string k_corner_br = "\xe2\x94\x98";         // U+2518 ┘
const std::string k_rounded_tl = "\xe2\x95\xad";        // U+256D ╭
const std::string k_rounded_br = "\xe2\x95\xaf";        // U+256F ╯
const std::string k_cross = "\xe2\x94\xbc";             // U+253C ┼
const std::string k_tee = "\xe2\x94\x9c";               // U+251C ├
const std::string k_full_block = "\xe2\x96\x88";        // U+2588 █
const std::string k_dark_shade = "\xe2\x96\x93";        // U+2593 ▓
const std::string k_light_shade = "\xe2\x96\x91";       // U+2591 ░

}  // namespace

// ---------------------------------------------------------------------------
// Normal case: each glyph class maps to its ASCII stand-in.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(full_box_set_maps_to_ascii)
{
    BOOST_TEST(transliterate_box_chars(k_light_horizontal) == "-");
    BOOST_TEST(transliterate_box_chars(k_heavy_horizontal) == "-");
    BOOST_TEST(transliterate_box_chars(k_double_horizontal) == "-");
    BOOST_TEST(transliterate_box_chars(k_light_vertical) == "|");
    BOOST_TEST(transliterate_box_chars(k_heavy_vertical) == "|");
    BOOST_TEST(transliterate_box_chars(k_corner_tl) == "+");
    BOOST_TEST(transliterate_box_chars(k_corner_br) == "+");
    BOOST_TEST(transliterate_box_chars(k_rounded_tl) == "+");
    BOOST_TEST(transliterate_box_chars(k_rounded_br) == "+");
    BOOST_TEST(transliterate_box_chars(k_cross) == "+");
    BOOST_TEST(transliterate_box_chars(k_tee) == "+");
    BOOST_TEST(transliterate_box_chars(k_full_block) == "#");
    BOOST_TEST(transliterate_box_chars(k_dark_shade) == "#");
    BOOST_TEST(transliterate_box_chars(k_light_shade) == "-");
}

// ---------------------------------------------------------------------------
// A full rounded-border line (as a window would draw it) becomes ASCII.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(mixed_box_line_becomes_ascii)
{
    const std::string line = k_rounded_tl + k_light_horizontal
                             + k_light_horizontal + k_rounded_br;
    BOOST_TEST(transliterate_box_chars(line) == "+--+");

    // A sparkline-style bar: filled blocks then an empty light-shade cell.
    const std::string bar = k_full_block + k_full_block + k_light_shade;
    BOOST_TEST(transliterate_box_chars(bar) == "##-");
}

// ---------------------------------------------------------------------------
// Edge case: box glyphs embedded in ordinary text leave the text alone.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(text_around_box_is_untouched)
{
    const std::string in = "a" + k_light_vertical + "b";
    BOOST_TEST(transliterate_box_chars(in) == "a|b");
    BOOST_TEST(transliterate_box_chars("plain ascii, no boxes")
               == "plain ascii, no boxes");
}

// ---------------------------------------------------------------------------
// Multibyte non-box text (accents, CJK, emoji) passes through unchanged.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(multibyte_text_unchanged)
{
    // "héllo 世界 🌍" (accent, CJK, emoji) - none are box/block glyphs.
    const std::string in =
        "h\xc3\xa9llo \xe4\xb8\x96\xe7\x95\x8c \xf0\x9f\x8c\x8d";
    BOOST_TEST(transliterate_box_chars(in) == in);
}

// ---------------------------------------------------------------------------
// Invalid-input: invalid and truncated UTF-8 pass through unchanged.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(invalid_and_truncated_utf8_passthrough)
{
    const std::string invalid = "a\xff\xfe""b";
    BOOST_TEST(transliterate_box_chars(invalid) == invalid);

    // A box-drawing lead byte with its tail chopped off: not decodable, so the
    // raw bytes survive rather than being mis-mapped.
    const std::string truncated = "x\xe2\x94";  // first two bytes of U+2500
    BOOST_TEST(transliterate_box_chars(truncated) == truncated);
}

// ---------------------------------------------------------------------------
// The mapping is idempotent: applying it twice equals applying it once.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(mapping_is_idempotent)
{
    const std::string in = k_rounded_tl + k_light_horizontal + k_tee + "abc"
                           + k_full_block + k_light_shade;
    const std::string once = transliterate_box_chars(in);
    BOOST_TEST(transliterate_box_chars(once) == once);
}
