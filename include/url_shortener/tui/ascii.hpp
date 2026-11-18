#pragma once

#include <string>

namespace url_shortener::tui
{

/**
 * @brief Transliterates Unicode box-drawing and block glyphs to ASCII.
 *
 * The pure, FTXUI-free mapping behind the @ref ThemeName::mono fallback (ADR
 * 0003 "ASCII-borders fallback" for legacy consoles and tmux). It decodes
 * @p utf8 one code point at a time and rewrites:
 *
 * - box-drawing glyphs (U+2500-U+257F): horizontal lines (light/heavy/double/
 *   dashed) to `-`, vertical lines to `|`, and every corner, tee, cross and
 *   rounded join to `+`;
 * - full/dark/medium shade and partial block elements (most of U+2580-U+259F,
 *   including U+2588 and U+2593) to `#`, the gauge/sparkline "filled" cell;
 * - the light shade U+2591 to `-`, the gauge/sparkline "empty" cell.
 *
 * Every other code point - ordinary ASCII, accents, CJK, emoji - is copied
 * through byte-for-byte, so only borders and bars change. Invalid UTF-8 bytes
 * and a truncated multibyte sequence at the end of input are passed through
 * unchanged (the function never throws). The mapping is idempotent: applying it
 * to its own output yields the same string, because every replacement is a
 * plain ASCII character that maps to itself.
 *
 * @param utf8 The text to transliterate; interpreted as UTF-8.
 * @return A copy with box-drawing and block glyphs replaced by ASCII, all other
 * bytes unchanged.
 */
std::string transliterate_box_chars(const std::string& utf8);

}  // namespace url_shortener::tui
