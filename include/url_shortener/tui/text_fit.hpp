#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace url_shortener::tui
{

/**
 * @file text_fit.hpp
 * @brief UTF-8-aware width fitting and the task-wide terminal-control
 * sanitizer for the TUI model layer (ADR 0003 Decision 2, threat model T-110).
 *
 * These helpers are FTXUI-free (they belong to `url_shortener_tui_model`) and
 * pure: no threads, no clocks, no I/O. @ref sanitize_for_terminal is the single
 * terminal-control choke point every render sink must apply to server/stored
 * strings *before* width fitting; @ref display_width and @ref truncate_to_width
 * then fit the already-safe text into a cell.
 */

/**
 * @brief Best-effort display width, in terminal columns, of a UTF-8 string.
 *
 * Decodes @p utf8 code point by code point and sums per-code-point widths:
 * wide East-Asian and emoji code points count as `2`, combining marks count as
 * `0`, and every other code point counts as `1`. Invalid UTF-8 bytes are each
 * counted as width `1` so the function never throws on malformed input (though
 * in production it runs after @ref sanitize_for_terminal, which yields valid
 * UTF-8). The width table is a documented approximation of a terminal's own
 * cell metrics; the UI library may prefer FTXUI's `string_width` and keep this
 * for the model's tests (see the subtask's "risks").
 *
 * @param utf8 The string to measure; interpreted as UTF-8.
 * @return The summed display width in columns; `0` for an empty string.
 */
std::size_t display_width(const std::string& utf8);

/**
 * @brief Truncates a UTF-8 string to at most @p width display columns,
 * appending an ellipsis (`...`) when content is cut.
 *
 * When @ref display_width of @p utf8 is already `<= width` the string is
 * returned unchanged (an exact fit is never truncated). Otherwise as many whole
 * code points as fit in `width - 3` columns are kept and `...` is appended, so
 * the result's display width never exceeds @p width. A UTF-8 sequence is never
 * split. When @p width is below the ellipsis width the result degrades to that
 * many dots (e.g. width `1` yields `"."`); @p width `0` yields an empty string.
 * The scan is `O(width)`: it stops as soon as the width budget is exceeded, so
 * a huge field is not walked in full (threat model T-121).
 *
 * @param utf8 The string to fit; interpreted as UTF-8.
 * @param width The maximum display width, in columns.
 * @return The original string when it fits, otherwise a truncated copy ending
 * in an ellipsis whose display width is `<= width`.
 */
std::string truncate_to_width(const std::string& utf8, std::size_t width);

/**
 * @brief Pads a UTF-8 string with spaces to exactly @p width display columns.
 *
 * When @ref display_width of @p utf8 is `>= width` the string is returned
 * unchanged (this function never truncates; call @ref truncate_to_width first).
 * Otherwise `width - display_width(utf8)` space characters are added: on the
 * right by default, or on the left when @p right_align is `true`.
 *
 * @param utf8 The string to pad; interpreted as UTF-8.
 * @param width The target display width, in columns.
 * @param right_align Pad on the left (right-justify the text) when `true`;
 * otherwise pad on the right (left-justify). Defaults to `false`.
 * @return The padded string, whose display width is `max(width,
 * display_width(utf8))`.
 */
std::string pad_to_width(const std::string& utf8,
                         std::size_t width,
                         bool right_align = false);

/**
 * @brief Neutralizes terminal-control bytes in untrusted text before it is
 * rendered (threat model T-110, CRITICAL).
 *
 * This is the one central sanitizer every TUI render sink must call on
 * server/stored strings (slugs, targets, metadata, job error text, row cells,
 * toast bodies) *before* width fitting, so a truncated cell can never re-expose
 * a half-stripped escape. It decodes @p in as UTF-8 and produces printable,
 * single-line text safe to hand to FTXUI `text()`. Specifically it:
 *
 * - drops `ESC` (0x1B) together with the entire payload of any CSI / OSC (BEL-
 *   or ST-terminated) / DCS / APC / PM / SOS sequence it introduces; an
 *   unterminated sequence is consumed to the end of input (bounded, no loop);
 * - maps every remaining C0 control (0x00-0x1F, including tab, newline and CR)
 *   to a single space;
 * - drops DEL (0x7F);
 * - drops C1 controls whether they arrive as raw bytes 0x80-0x9F (always
 *   invalid UTF-8 in a leading position) or as the code points U+0080-U+009F;
 * - drops bidirectional-format controls (U+202A-U+202E, U+2066-U+2069, U+200E,
 *   U+200F, U+061C) and zero-width / invisible code points (U+200B-U+200D,
 *   U+2060, U+FEFF);
 * - replaces every invalid UTF-8 byte with U+FFFD (the replacement character);
 * - caps the result at @p max_bytes bytes, never splitting a UTF-8 sequence at
 *   the cut.
 *
 * The scan is a single `O(bytes)` pass with output bounded by @p max_bytes; it
 * is pure and FTXUI-free. Note that confirm-token *comparison* (the typed-slug
 * delete of subtask 06) must run against the raw bytes; only the *displayed*
 * token is passed through this function, so a bidi override cannot make the
 * dialog show a different slug than the bytes being compared.
 *
 * @param in The untrusted text to sanitize; interpreted as UTF-8.
 * @param max_bytes Maximum size of the returned string in bytes; a code point
 * that would cross this bound is dropped whole rather than split. Defaults to
 * 64 KiB.
 * @return Printable, single-line, valid UTF-8 text containing no `ESC`, C0
 * (except spaces), DEL, C1, bidi or zero-width code points, at most
 * @p max_bytes bytes long.
 */
std::string sanitize_for_terminal(std::string_view in,
                                  std::size_t max_bytes = 64 * 1024);

}  // namespace url_shortener::tui
