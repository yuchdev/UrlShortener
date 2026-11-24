#include "url_shortener/tui/ascii.hpp"

#include <cstdint>

namespace url_shortener::tui
{

namespace
{

/// Returns the ASCII replacement for a box-drawing / block code point, or
/// `'\0'` when @p cp is not a glyph this mapping touches.
///
/// Box-drawing (U+2500-U+257F) splits into horizontal lines (`-`), vertical
/// lines (`|`) and everything else - corners, tees, crosses, rounded joins -
/// which become `+`. Block elements (U+2580-U+259F) become `#` except the light
/// shade U+2591, which becomes `-` so an "empty" gauge cell is visibly lighter
/// than a filled one.
char box_ascii(std::uint32_t cp)
{
    // Block elements: full/partial blocks and shades.
    if (cp == 0x2591u)
    {
        return '-';  // light shade -> empty gauge/sparkline cell
    }
    if (cp >= 0x2580u && cp <= 0x259Fu)
    {
        return '#';  // full/dark/medium shade and eighth/partial blocks
    }

    if (cp < 0x2500u || cp > 0x257Fu)
    {
        return '\0';  // not a box/block glyph
    }

    switch (cp)
    {
    // Horizontal lines: light, heavy, dashed (various densities), double, and
    // the single-side light/heavy stubs that run left-right.
    case 0x2500u:
    case 0x2501u:
    case 0x2504u:
    case 0x2505u:
    case 0x2508u:
    case 0x2509u:
    case 0x254Cu:
    case 0x254Du:
    case 0x2550u:
    case 0x2574u:
    case 0x2576u:
    case 0x2578u:
    case 0x257Au:
    case 0x257Cu:
    case 0x257Eu:
        return '-';

    // Vertical lines: light, heavy, dashed, double, and the up/down stubs.
    case 0x2502u:
    case 0x2503u:
    case 0x2506u:
    case 0x2507u:
    case 0x250Au:
    case 0x250Bu:
    case 0x254Eu:
    case 0x254Fu:
    case 0x2551u:
    case 0x2575u:
    case 0x2577u:
    case 0x2579u:
    case 0x257Bu:
    case 0x257Du:
    case 0x257Fu:
        return '|';

    // Everything else in the block is a corner, tee, cross or rounded join.
    default:
        return '+';
    }
}

/// Decodes one UTF-8 code point from @p s starting at @p i.
///
/// @param s The UTF-8 bytes.
/// @param i The byte offset to decode from; must be `< s.size()`.
/// @param[out] length Receives the number of bytes consumed: the full sequence
/// length on success, or `1` for an invalid lead byte or a sequence truncated
/// by the end of input (so the caller advances and copies that single raw byte).
/// @return The decoded code point on success; `0xFFFFFFFF` when the bytes at
/// @p i are not a well-formed UTF-8 sequence (the caller then passes the raw
/// byte through unchanged).
std::uint32_t decode_utf8(const std::string& s, std::size_t i,
                          std::size_t& length)
{
    const auto b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80u)
    {
        length = 1;
        return b0;
    }

    std::size_t need = 0;
    std::uint32_t cp = 0;
    if ((b0 & 0xE0u) == 0xC0u)
    {
        need = 1;
        cp = b0 & 0x1Fu;
    }
    else if ((b0 & 0xF0u) == 0xE0u)
    {
        need = 2;
        cp = b0 & 0x0Fu;
    }
    else if ((b0 & 0xF8u) == 0xF0u)
    {
        need = 3;
        cp = b0 & 0x07u;
    }
    else
    {
        length = 1;
        return 0xFFFFFFFFu;  // invalid lead byte
    }

    if (i + need >= s.size())
    {
        length = 1;
        return 0xFFFFFFFFu;  // truncated sequence at end of input
    }
    for (std::size_t k = 1; k <= need; ++k)
    {
        const auto cb = static_cast<unsigned char>(s[i + k]);
        if ((cb & 0xC0u) != 0x80u)
        {
            length = 1;
            return 0xFFFFFFFFu;  // bad continuation byte
        }
        cp = (cp << 6) | (cb & 0x3Fu);
    }
    length = need + 1;
    return cp;
}

}  // namespace

std::string transliterate_box_chars(const std::string& utf8)
{
    std::string out;
    out.reserve(utf8.size());

    std::size_t i = 0;
    while (i < utf8.size())
    {
        std::size_t length = 1;
        const std::uint32_t cp = decode_utf8(utf8, i, length);
        if (cp != 0xFFFFFFFFu)
        {
            if (const char ascii = box_ascii(cp))
            {
                out.push_back(ascii);
                i += length;
                continue;
            }
        }
        // Not a box/block glyph (or invalid UTF-8): copy the raw bytes.
        out.append(utf8, i, length);
        i += length;
    }
    return out;
}

}  // namespace url_shortener::tui
