#define BOOST_TEST_MODULE TuiTextFitTest

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/tui/text_fit.hpp>

#include "hostile_strings.hpp"

using url_shortener::tui::display_width;
using url_shortener::tui::pad_to_width;
using url_shortener::tui::sanitize_for_terminal;
using url_shortener::tui::truncate_to_width;
using url_shortener::tui::testing::combining_flood;
using url_shortener::tui::testing::has_display_hazard;
using url_shortener::tui::testing::has_terminal_hazard;
using url_shortener::tui::testing::hostile_samples;
using url_shortener::tui::testing::zalgo_cluster;

namespace
{

// Decodes @p s as UTF-8, asserting validity, and returns the code points. A
// decoding failure fails the current test (used to prove sanitizer output is
// always valid UTF-8).
std::vector<std::uint32_t> decode_all_or_fail(std::string_view s)
{
    std::vector<std::uint32_t> out;
    std::size_t i = 0;
    while (i < s.size())
    {
        const auto b = static_cast<unsigned char>(s[i]);
        std::size_t need = 0;
        std::uint32_t cp = 0;
        std::uint32_t lowest = 0;
        if (b < 0x80u)
        {
            out.push_back(b);
            ++i;
            continue;
        }
        else if ((b & 0xE0u) == 0xC0u)
        {
            need = 1;
            cp = b & 0x1Fu;
            lowest = 0x80u;
        }
        else if ((b & 0xF0u) == 0xE0u)
        {
            need = 2;
            cp = b & 0x0Fu;
            lowest = 0x800u;
        }
        else if ((b & 0xF8u) == 0xF0u)
        {
            need = 3;
            cp = b & 0x07u;
            lowest = 0x10000u;
        }
        else
        {
            BOOST_FAIL("invalid UTF-8 leading byte in sanitized output");
        }
        BOOST_REQUIRE_MESSAGE(i + need < s.size(),
                              "truncated UTF-8 sequence in output");
        for (std::size_t k = 1; k <= need; ++k)
        {
            const auto cont = static_cast<unsigned char>(s[i + k]);
            BOOST_REQUIRE_MESSAGE((cont & 0xC0u) == 0x80u,
                                  "bad continuation byte in output");
            cp = (cp << 6) | (cont & 0x3Fu);
        }
        BOOST_REQUIRE_MESSAGE(cp >= lowest && cp <= 0x10FFFFu &&
                                  !(cp >= 0xD800u && cp <= 0xDFFFu),
                              "overlong/surrogate/out-of-range code point");
        out.push_back(cp);
        i += need + 1;
    }
    return out;
}

// Asserts sanitized output is valid UTF-8 and carries no display hazard (the
// authoritative sink check: control bytes, bidi, zero-width, spoofing format
// code points and over-cap combining runs).
void assert_output_is_safe(const std::string& out)
{
    BOOST_TEST(!has_terminal_hazard(out));
    BOOST_TEST(!has_display_hazard(out));
    decode_all_or_fail(out);  // also asserts the output is valid UTF-8
}

}  // namespace

// ---------------------------------------------------------------------------
// display_width
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(display_width_ascii_is_byte_count)
{
    BOOST_TEST(display_width("") == 0u);
    BOOST_TEST(display_width("hello") == 5u);
}

BOOST_AUTO_TEST_CASE(display_width_multibyte_accent_is_one)
{
    // "héllo": the accented e is U+00E9 (2 bytes, width 1).
    BOOST_TEST(display_width("h\xc3\xa9llo") == 5u);
}

BOOST_AUTO_TEST_CASE(display_width_cjk_is_two_each)
{
    // U+4E16 U+754C each display as width 2.
    BOOST_TEST(display_width("\xe4\xb8\x96\xe7\x95\x8c") == 4u);
}

BOOST_AUTO_TEST_CASE(display_width_emoji_is_two)
{
    // U+1F30D earth globe, width 2.
    BOOST_TEST(display_width("\xf0\x9f\x8c\x8d") == 2u);
}

BOOST_AUTO_TEST_CASE(display_width_combining_mark_is_zero)
{
    // 'e' + U+0301 combining acute accent.
    BOOST_TEST(display_width("e\xcc\x81") == 1u);
}

BOOST_AUTO_TEST_CASE(display_width_invalid_bytes_do_not_throw)
{
    BOOST_TEST(display_width(std::string("a\xff\xfe""b")) >= 2u);
}

// ---------------------------------------------------------------------------
// truncate_to_width
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(truncate_exact_fit_is_unchanged)
{
    BOOST_TEST(truncate_to_width("hello", 5) == "hello");
}

BOOST_AUTO_TEST_CASE(truncate_wider_appends_ellipsis)
{
    BOOST_TEST(truncate_to_width("abcdefgh", 6) == "abc...");
}

BOOST_AUTO_TEST_CASE(truncate_small_widths_never_exceed)
{
    BOOST_TEST(truncate_to_width("abcdef", 0) == "");
    BOOST_TEST(truncate_to_width("abcdef", 1) == ".");
    BOOST_TEST(truncate_to_width("abcdef", 2) == "..");
    BOOST_TEST(truncate_to_width("abcdef", 3) == "...");
    for (std::size_t w = 0; w <= 8; ++w)
    {
        BOOST_TEST(display_width(truncate_to_width("abcdefgh", w)) <= w);
    }
}

BOOST_AUTO_TEST_CASE(truncate_never_splits_a_codepoint)
{
    // Three CJK code points (width 2 each) truncated to width 5: budget is 2
    // columns before "...", so exactly one whole CJK char survives.
    const std::string cjk = "\xe4\xb8\x96\xe7\x95\x8c\xe4\xb8\x96";
    const std::string out = truncate_to_width(cjk, 5);
    BOOST_TEST(out == "\xe4\xb8\x96...");
    BOOST_TEST(display_width(out) <= 5u);
}

BOOST_AUTO_TEST_CASE(truncate_invalid_utf8_does_not_crash)
{
    const std::string out =
        truncate_to_width(std::string("a\xff\xfe" "b\xfe"), 3);
    BOOST_TEST(display_width(out) <= 3u);
}

// ---------------------------------------------------------------------------
// pad_to_width
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(pad_adds_trailing_spaces_by_default)
{
    BOOST_TEST(pad_to_width("ab", 5) == "ab   ");
}

BOOST_AUTO_TEST_CASE(pad_right_align_prepends_spaces)
{
    BOOST_TEST(pad_to_width("ab", 5, true) == "   ab");
}

BOOST_AUTO_TEST_CASE(pad_wider_content_is_unchanged)
{
    BOOST_TEST(pad_to_width("abcdef", 3) == "abcdef");
}

// ---------------------------------------------------------------------------
// sanitize_for_terminal (T-110)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(sanitize_benign_unicode_passes_through)
{
    const std::string benign =
        "h\xc3\xa9llo \xe4\xb8\x96\xe7\x95\x8c \xf0\x9f\x8c\x8d";
    BOOST_TEST(sanitize_for_terminal(benign) == benign);
}

BOOST_AUTO_TEST_CASE(sanitize_drops_csi_erase_screen)
{
    const std::string out = sanitize_for_terminal("before\x1b[2Jafter");
    BOOST_TEST(out == "beforeafter");
    assert_output_is_safe(out);
}

BOOST_AUTO_TEST_CASE(sanitize_drops_osc52_clipboard_bel_and_st)
{
    BOOST_TEST(sanitize_for_terminal("x\x1b]52;c;aGk=\x07y") == "xy");
    BOOST_TEST(sanitize_for_terminal("x\x1b]52;c;aGk=\x1b\\y") == "xy");
}

BOOST_AUTO_TEST_CASE(sanitize_drops_dcs_apc_pm_sos_payloads)
{
    BOOST_TEST(sanitize_for_terminal("a\x1b""Ppayload\x1b\\b") == "ab");
    BOOST_TEST(sanitize_for_terminal("a\x1b_data\x1b\\b") == "ab");
    BOOST_TEST(sanitize_for_terminal("a\x1b^msg\x1b\\b") == "ab");
    BOOST_TEST(sanitize_for_terminal("a\x1bXsos\x1b\\b") == "ab");
}

BOOST_AUTO_TEST_CASE(sanitize_unterminated_sequence_is_bounded)
{
    // An OSC with no terminator must consume to the end, not loop or overflow.
    const std::string out = sanitize_for_terminal("keep\x1b]52;c;unterminated");
    BOOST_TEST(out == "keep");
    assert_output_is_safe(out);
    // Unterminated CSI likewise.
    BOOST_TEST(sanitize_for_terminal("keep\x1b[") == "keep");
}

BOOST_AUTO_TEST_CASE(sanitize_maps_c0_whitespace_to_spaces)
{
    BOOST_TEST(sanitize_for_terminal("a\tb\nc\rd") == "a b c d");
}

BOOST_AUTO_TEST_CASE(sanitize_drops_del)
{
    BOOST_TEST(sanitize_for_terminal(std::string("a\x7f""b")) == "ab");
}

BOOST_AUTO_TEST_CASE(sanitize_drops_raw_c1_byte)
{
    // 0x9B (8-bit CSI) is dropped; the following "2J" is left as inert text.
    BOOST_TEST(sanitize_for_terminal(std::string("a\x9b""2Jb")) == "a2Jb");
}

BOOST_AUTO_TEST_CASE(sanitize_drops_c1_codepoint)
{
    // U+009F encoded as 0xC2 0x9F.
    BOOST_TEST(sanitize_for_terminal(std::string("a\xc2\x9f""b")) == "ab");
}

BOOST_AUTO_TEST_CASE(sanitize_strips_bidi_override)
{
    const std::string out = sanitize_for_terminal("ab\xe2\x80\xae""cd");
    BOOST_TEST(out == "abcd");
    assert_output_is_safe(out);
}

BOOST_AUTO_TEST_CASE(sanitize_strips_zero_width_and_bom)
{
    BOOST_TEST(sanitize_for_terminal("a\xe2\x80\x8b""b") == "ab");  // U+200B
    BOOST_TEST(sanitize_for_terminal("\xef\xbb\xbf""x") == "x");    // U+FEFF
}

BOOST_AUTO_TEST_CASE(sanitize_replaces_invalid_utf8_with_fffd)
{
    const std::string out = sanitize_for_terminal(std::string("a\xff" "b"));
    // 'a' + U+FFFD + 'b'
    BOOST_TEST(out == std::string("a\xef\xbf\xbd""b"));
    assert_output_is_safe(out);
}

BOOST_AUTO_TEST_CASE(sanitize_caps_output_and_never_splits_a_sequence)
{
    // 64 KiB of ASCII bounded to 16 bytes.
    const std::string big(64u * 1024u, 'A');
    const std::string out = sanitize_for_terminal(big, 16);
    BOOST_TEST(out.size() == 16u);

    // A 3-byte CJK char that would cross the cap is dropped whole, not split.
    const std::string cjk = "\xe4\xb8\x96\xe4\xb8\x96";  // two width-2 chars
    const std::string capped = sanitize_for_terminal(cjk, 4);
    BOOST_TEST(capped.size() == 3u);  // only the first whole char fits
    assert_output_is_safe(capped);
}

BOOST_AUTO_TEST_CASE(sanitize_maps_line_separators_to_space)
{
    // U+2028 and U+2029 collapse to spaces like other line breaks.
    const std::string out =
        sanitize_for_terminal("a\xe2\x80\xa8""b\xe2\x80\xa9""c");
    BOOST_TEST(out == "a b c");
    assert_output_is_safe(out);
}

BOOST_AUTO_TEST_CASE(sanitize_strips_invisible_format_marks)
{
    BOOST_TEST(sanitize_for_terminal("ad\xc2\xad""min") == "admin");  // U+00AD
    // U+180E Mongolian vowel sep + U+034F combining grapheme joiner.
    BOOST_TEST(sanitize_for_terminal("a\xe1\xa0\x8e""b\xcd\x8f""c") == "abc");
    // U+FE0F variation selector.
    BOOST_TEST(sanitize_for_terminal("x\xef\xb8\x8f""y") == "xy");
    // U+E0041 tag character.
    BOOST_TEST(sanitize_for_terminal("a\xf3\xa0\x81\x81""b") == "ab");
}

BOOST_AUTO_TEST_CASE(sanitize_caps_combining_run_per_base)
{
    // One base 'a' with 40 stacked combining marks, then 'b'.
    const std::string out = sanitize_for_terminal(zalgo_cluster());
    // At most four combining marks survive per base: 'a' + 4 marks + 'b'.
    BOOST_TEST(out == std::string("a") + "\xcc\x81\xcc\x81\xcc\x81\xcc\x81" +
                          "b");
    BOOST_TEST(display_width(out) == 2u);
    assert_output_is_safe(out);
}

BOOST_AUTO_TEST_CASE(sanitize_bounds_a_64kib_combining_flood)
{
    // A ~64 KiB field of nothing but combining marks collapses to the per-base
    // cap (no base yet, so the leading run is capped too): at most 4 marks.
    const std::string out = sanitize_for_terminal(combining_flood());
    BOOST_TEST(out.size() <= 4u * 2u);  // <= 4 two-byte marks
    BOOST_TEST(display_width(out) == 0u);
    assert_output_is_safe(out);
}

BOOST_AUTO_TEST_CASE(truncate_is_bounded_on_raw_combining_flood)
{
    // truncate_to_width must stay within the width/time bound even when handed
    // an un-sanitized zero-width flood (its columns nominally "fit").
    const std::string flood = combining_flood();
    const std::string out = truncate_to_width(flood, 40);
    BOOST_TEST(display_width(out) <= 40u);
    // Bounded well below the 64 KiB input: O(width) code points plus ellipsis.
    BOOST_TEST(out.size() <= (40u * 8u + 32u) * 2u + 3u);
}

BOOST_AUTO_TEST_CASE(has_display_hazard_flags_raw_and_clears_sanitized)
{
    for (const auto& sample : hostile_samples())
    {
        BOOST_TEST_INFO("sample=" << sample.name);
        // The big_field/benign-ish samples contain no hazard even raw, so only
        // assert the sanitized form is clean here.
        const std::string safe = sanitize_for_terminal(sample.raw);
        BOOST_TEST(!has_display_hazard(safe));
    }
    // A raw bidi override and a raw over-cap combining run are both flagged.
    BOOST_TEST(has_display_hazard("ab\xe2\x80\xae""cd"));         // U+202E
    BOOST_TEST(has_display_hazard(zalgo_cluster()));             // 40 marks
    BOOST_TEST(has_display_hazard("x\xef\xb8\x8f""y"));           // U+FE0F
}

BOOST_AUTO_TEST_CASE(sanitize_handles_every_hostile_sample)
{
    for (const auto& sample : hostile_samples())
    {
        const std::string out = sanitize_for_terminal(sample.raw);
        BOOST_TEST_INFO("sample=" << sample.name);
        assert_output_is_safe(out);
    }
}

BOOST_AUTO_TEST_CASE(sanitize_then_truncate_never_reexposes_control)
{
    for (const auto& sample : hostile_samples())
    {
        const std::string safe = sanitize_for_terminal(sample.raw);
        for (std::size_t w : {0u, 1u, 3u, 5u, 8u, 40u})
        {
            const std::string fitted = truncate_to_width(safe, w);
            BOOST_TEST_INFO("sample=" << sample.name << " width=" << w);
            BOOST_TEST(!has_display_hazard(fitted));
        }
    }
}

BOOST_AUTO_TEST_CASE(sanitize_fuzz_random_bytes_always_safe)
{
    std::mt19937 rng(0xC0FFEEu);
    std::uniform_int_distribution<int> byte(0, 255);
    std::uniform_int_distribution<int> len(0, 512);
    for (int iter = 0; iter < 2000; ++iter)
    {
        std::string in;
        const int n = len(rng);
        in.reserve(static_cast<std::size_t>(n));
        for (int k = 0; k < n; ++k)
        {
            in.push_back(static_cast<char>(byte(rng)));
        }
        const std::string out = sanitize_for_terminal(in);
        BOOST_TEST_INFO("iter=" << iter);
        assert_output_is_safe(out);
    }
}
