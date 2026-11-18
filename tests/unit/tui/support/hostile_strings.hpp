#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file hostile_strings.hpp
 * @brief Shared hostile-data fixture for the TUI terminal-control sanitizer
 * (threat model T-110, owned by subtask 09).
 *
 * This header is the single source of adversarial strings every render-sink
 * test (10, 08, 06, 11, 13, 14, 16) reuses to prove that no terminal-control
 * byte reaches the rendered grid. Each sample is an *untrusted* stored value as
 * it might arrive from a legacy row, `metadata`/`tags`/`campaign`, a slug on a
 * pre-fix row, a `db.rows` `referrer`/`user_agent`, or a job's failure text.
 * The strings here are deliberately dangerous; they are only ever passed
 * through `sanitize_for_terminal` under test, never emitted to a real terminal.
 */

namespace url_shortener::tui::testing
{

/**
 * @brief One named hostile input and the class of hazard it represents.
 */
struct HostileSample
{
    ///< Short identifier used in test failure messages.
    std::string name;

    ///< The raw, untrusted bytes to feed through the sanitizer.
    std::string raw;
};

/// ESC + CSI "erase entire screen" (`ESC [ 2 J`).
inline std::string esc_clear_screen()
{
    return std::string("\x1b[2J");
}

/// ESC + CSI cursor-home plus SGR reset, a screen-repaint primitive.
inline std::string esc_cursor_home()
{
    return std::string("\x1b[H\x1b[0m");
}

/// OSC 52 clipboard write (BEL terminated): overwrites the system clipboard.
inline std::string osc52_clipboard_bel()
{
    return std::string("\x1b]52;c;aGVsbG8=\x07");
}

/// OSC 52 clipboard write (ST terminated: ESC '\\').
inline std::string osc52_clipboard_st()
{
    return std::string("\x1b]52;c;aGVsbG8=\x1b\\");
}

/// OSC 0 window-title change (BEL terminated).
inline std::string osc_window_title()
{
    return std::string("\x1b]0;pwned\x07");
}

/// DCS sequence (ST terminated) with an embedded payload.
inline std::string dcs_sequence()
{
    return std::string("\x1b""Pqpayload\x1b\\end");
}

/// APC sequence (ST terminated).
inline std::string apc_sequence()
{
    return std::string("\x1b_cmd\x1b\\tail");
}

/// U+202E right-to-left override embedded in otherwise benign text; can make a
/// confirm dialog display a different slug than the bytes typed.
inline std::string bidi_override_slug()
{
    return std::string("ab\xe2\x80\xae""cd-promo");
}

/// U+200B zero-width space hidden between visible characters.
inline std::string zero_width_space()
{
    return std::string("a\xe2\x80\x8b""b\xe2\x80\x8b""c");
}

/// U+FEFF byte-order mark / zero-width no-break space.
inline std::string zero_width_bom()
{
    return std::string("\xef\xbb\xbf""text");
}

/// A raw C1 control byte (0x9B, 8-bit CSI) with trailing bytes.
inline std::string raw_c1_byte()
{
    return std::string("a\x9b""2Jb");
}

/// U+009F expressed as the two-byte code point 0xC2 0x9F (C1 as a code point).
inline std::string c1_codepoint()
{
    return std::string("a\xc2\x9f""b");
}

/// Invalid UTF-8 bytes that must become U+FFFD.
inline std::string invalid_utf8()
{
    return std::string("a\xff\xfe\x80""b");
}

/// Tab, newline and carriage return that must each collapse to one space.
inline std::string c0_whitespace()
{
    return std::string("a\tb\nc\rd");
}

/// A 64 KiB benign field used to prove the sanitizer bounds its output.
inline std::string big_field(std::size_t bytes = 64u * 1024u)
{
    return std::string(bytes, 'A');
}

/// Benign multibyte text (accent, CJK, emoji) that must pass through unchanged.
inline std::string benign_unicode()
{
    return std::string("h\xc3\xa9llo \xe4\xb8\x96\xe7\x95\x8c \xf0\x9f\x8c\x8d");
}

/**
 * @brief The full set of hostile samples for loop-based assertions.
 *
 * @return One @ref HostileSample per hazard class plus representative mixtures.
 */
inline std::vector<HostileSample> hostile_samples()
{
    return {
        {"esc_clear_screen", esc_clear_screen()},
        {"esc_cursor_home", esc_cursor_home()},
        {"osc52_clipboard_bel", osc52_clipboard_bel()},
        {"osc52_clipboard_st", osc52_clipboard_st()},
        {"osc_window_title", osc_window_title()},
        {"dcs_sequence", dcs_sequence()},
        {"apc_sequence", apc_sequence()},
        {"bidi_override_slug", bidi_override_slug()},
        {"zero_width_space", zero_width_space()},
        {"zero_width_bom", zero_width_bom()},
        {"raw_c1_byte", raw_c1_byte()},
        {"c1_codepoint", c1_codepoint()},
        {"invalid_utf8", invalid_utf8()},
        {"c0_whitespace", c0_whitespace()},
    };
}

/**
 * @brief Predicate the render-sink tests apply to any *rendered* string to
 * assert the sanitizer did its job.
 *
 * Reports whether @p rendered still contains a terminal-control hazard: an
 * `ESC` (0x1B), any C0 control other than space, DEL (0x7F), or a C1 control
 * (0x80-0x9F) whether it arrives as a raw leading byte or as a decoded code
 * point. It is UTF-8 aware so a legitimate multibyte sequence's continuation
 * bytes (which fall in 0x80-0xBF) are never mistaken for a raw C1 byte. The
 * sinks reuse this so the T-110 assertion reads the same everywhere; bidi /
 * zero-width spoofing code points are checked separately by each test.
 *
 * @param rendered The text produced after sanitization (and any width fitting).
 * @return `true` when a forbidden control byte or C1 code point is present.
 */
inline bool has_terminal_hazard(std::string_view rendered)
{
    std::size_t i = 0;
    while (i < rendered.size())
    {
        const auto b = static_cast<unsigned char>(rendered[i]);
        if (b < 0x80u)
        {
            if (b == 0x1Bu || b == 0x7Fu || (b < 0x20u && b != 0x20u))
            {
                return true;  // ESC / DEL / C0 control (space is allowed)
            }
            ++i;
            continue;
        }

        // Decode one multibyte code point; a raw C1 or invalid lead is a hazard.
        std::size_t need = 0;
        std::uint32_t cp = 0;
        if ((b & 0xE0u) == 0xC0u)
        {
            need = 1;
            cp = b & 0x1Fu;
        }
        else if ((b & 0xF0u) == 0xE0u)
        {
            need = 2;
            cp = b & 0x0Fu;
        }
        else if ((b & 0xF8u) == 0xF0u)
        {
            need = 3;
            cp = b & 0x07u;
        }
        else
        {
            return true;  // raw C1 (0x80-0x9F) or otherwise invalid lead byte
        }
        if (i + need >= rendered.size())
        {
            return true;  // truncated sequence
        }
        for (std::size_t k = 1; k <= need; ++k)
        {
            cp = (cp << 6) |
                 (static_cast<unsigned char>(rendered[i + k]) & 0x3Fu);
        }
        if (cp >= 0x0080u && cp <= 0x009Fu)
        {
            return true;  // C1 control as a code point
        }
        i += need + 1;
    }
    return false;
}

}  // namespace url_shortener::tui::testing
