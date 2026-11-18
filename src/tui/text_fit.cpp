#include <url_shortener/tui/text_fit.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace url_shortener::tui
{

namespace
{

///< Unicode scalar value of the replacement character U+FFFD.
constexpr std::uint32_t k_replacement = 0xFFFDu;

/**
 * @brief Decodes one UTF-8 code point starting at @p pos in @p s.
 *
 * @param s The byte string being scanned.
 * @param pos Index of the leading byte; must be `< s.size()`.
 * @param[out] cp The decoded scalar value, or @ref k_replacement when the bytes
 * at @p pos are not a valid, shortest-form, non-surrogate UTF-8 sequence.
 * @param[out] len Number of input bytes consumed: the sequence length on
 * success, or `1` on an invalid leading/continuation byte so the scan always
 * advances.
 */
void decode_utf8(std::string_view s,
                 std::size_t pos,
                 std::uint32_t& cp,
                 std::size_t& len)
{
    const auto byte = static_cast<unsigned char>(s[pos]);
    if (byte < 0x80u)
    {
        cp = byte;
        len = 1;
        return;
    }

    std::size_t need = 0;
    std::uint32_t value = 0;
    std::uint32_t lowest = 0;
    if ((byte & 0xE0u) == 0xC0u)
    {
        need = 1;
        value = byte & 0x1Fu;
        lowest = 0x80u;
    }
    else if ((byte & 0xF0u) == 0xE0u)
    {
        need = 2;
        value = byte & 0x0Fu;
        lowest = 0x800u;
    }
    else if ((byte & 0xF8u) == 0xF0u)
    {
        need = 3;
        value = byte & 0x07u;
        lowest = 0x10000u;
    }
    else
    {
        cp = k_replacement;
        len = 1;
        return;
    }

    if (pos + need >= s.size())
    {
        cp = k_replacement;
        len = 1;
        return;
    }

    for (std::size_t k = 1; k <= need; ++k)
    {
        const auto cont = static_cast<unsigned char>(s[pos + k]);
        if ((cont & 0xC0u) != 0x80u)
        {
            cp = k_replacement;
            len = 1;
            return;
        }
        value = (value << 6) | (cont & 0x3Fu);
    }

    if (value < lowest || value > 0x10FFFFu ||
        (value >= 0xD800u && value <= 0xDFFFu))
    {
        cp = k_replacement;
        len = 1;
        return;
    }

    cp = value;
    len = need + 1;
}

/**
 * @brief Reports whether a code point must be stripped by
 * @ref sanitize_for_terminal: C1 controls, bidirectional-format controls and
 * zero-width / invisible code points.
 *
 * @param cp A decoded Unicode scalar value.
 * @return `true` when @p cp is forbidden and must be dropped.
 */
bool is_forbidden_codepoint(std::uint32_t cp)
{
    // C1 controls as code points (e.g. encoded as 0xC2 0x80..0xC2 0x9F).
    if (cp >= 0x0080u && cp <= 0x009Fu)
    {
        return true;
    }
    // Arabic letter mark.
    if (cp == 0x061Cu)
    {
        return true;
    }
    // Zero-width space / non-joiner / joiner.
    if (cp >= 0x200Bu && cp <= 0x200Du)
    {
        return true;
    }
    // Left-to-right / right-to-left marks.
    if (cp == 0x200Eu || cp == 0x200Fu)
    {
        return true;
    }
    // Bidi embeddings / overrides / pop.
    if (cp >= 0x202Au && cp <= 0x202Eu)
    {
        return true;
    }
    // Word joiner.
    if (cp == 0x2060u)
    {
        return true;
    }
    // Bidi isolates.
    if (cp >= 0x2066u && cp <= 0x2069u)
    {
        return true;
    }
    // Zero-width no-break space / byte-order mark.
    if (cp == 0xFEFFu)
    {
        return true;
    }
    return false;
}

/**
 * @brief Best-effort terminal column width of a single code point.
 *
 * @param cp A decoded Unicode scalar value.
 * @return `0` for combining marks, `2` for wide East-Asian / emoji ranges,
 * otherwise `1`.
 */
std::size_t codepoint_width(std::uint32_t cp)
{
    // Combining diacritical marks (best-effort zero-width set).
    if ((cp >= 0x0300u && cp <= 0x036Fu) ||
        (cp >= 0x1AB0u && cp <= 0x1AFFu) ||
        (cp >= 0x1DC0u && cp <= 0x1DFFu) ||
        (cp >= 0x20D0u && cp <= 0x20FFu) ||
        (cp >= 0xFE20u && cp <= 0xFE2Fu))
    {
        return 0;
    }

    // Wide / fullwidth ranges and emoji.
    if ((cp >= 0x1100u && cp <= 0x115Fu) ||  // Hangul Jamo
        (cp >= 0x2E80u && cp <= 0x303Eu) ||  // CJK radicals .. punctuation
        (cp >= 0x3041u && cp <= 0x33FFu) ||  // Kana .. CJK symbols
        (cp >= 0x3400u && cp <= 0x4DBFu) ||  // CJK ext A
        (cp >= 0x4E00u && cp <= 0x9FFFu) ||  // CJK unified
        (cp >= 0xA000u && cp <= 0xA4CFu) ||  // Yi
        (cp >= 0xAC00u && cp <= 0xD7A3u) ||  // Hangul syllables
        (cp >= 0xF900u && cp <= 0xFAFFu) ||  // CJK compat
        (cp >= 0xFE30u && cp <= 0xFE4Fu) ||  // CJK compat forms
        (cp >= 0xFF00u && cp <= 0xFF60u) ||  // Fullwidth forms
        (cp >= 0xFFE0u && cp <= 0xFFE6u) ||  // Fullwidth signs
        (cp >= 0x1F300u && cp <= 0x1FAFFu) ||  // emoji / symbols
        (cp >= 0x20000u && cp <= 0x3FFFDu))  // CJK ext B+
    {
        return 2;
    }

    return 1;
}

/**
 * @brief Appends the UTF-8 encoding of @p cp to @p out.
 *
 * @param cp A valid Unicode scalar value (`<= U+10FFFF`, non-surrogate).
 * @param[out] out The string the encoded bytes are appended to.
 */
void append_utf8(std::uint32_t cp, std::string& out)
{
    if (cp < 0x80u)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp < 0x800u)
    {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
    else if (cp < 0x10000u)
    {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

/**
 * @brief Consumes an escape sequence introduced by `ESC` at @p pos and returns
 * the index just past it. The sequence is dropped by the caller; this only
 * advances over its bytes.
 *
 * @param s The byte string being scanned.
 * @param pos Index of the `ESC` (0x1B) byte.
 * @return The index of the first byte after the sequence (bounded by
 * `s.size()`), so an unterminated sequence consumes the rest of the input.
 */
std::size_t skip_escape_sequence(std::string_view s, std::size_t pos)
{
    const std::size_t n = s.size();
    std::size_t i = pos + 1;  // past ESC
    if (i >= n)
    {
        return i;
    }

    const auto intro = static_cast<unsigned char>(s[i]);
    if (intro == 0x5Bu)  // CSI: ESC [
    {
        ++i;
        while (i < n)
        {
            const auto b = static_cast<unsigned char>(s[i]);
            if (b >= 0x20u && b <= 0x3Fu)  // params / intermediates
            {
                ++i;
                continue;
            }
            break;
        }
        if (i < n)
        {
            const auto b = static_cast<unsigned char>(s[i]);
            if (b >= 0x40u && b <= 0x7Eu)  // final byte
            {
                ++i;
            }
        }
        return i;
    }

    if (intro == 0x5Du)  // OSC: ESC ] ... (BEL or ST terminated)
    {
        ++i;
        while (i < n)
        {
            const auto b = static_cast<unsigned char>(s[i]);
            if (b == 0x07u)  // BEL
            {
                return i + 1;
            }
            if (b == 0x9Cu)  // 8-bit ST
            {
                return i + 1;
            }
            if (b == 0x1Bu && i + 1 < n &&
                static_cast<unsigned char>(s[i + 1]) == 0x5Cu)  // ESC \ ST
            {
                return i + 2;
            }
            ++i;
        }
        return i;
    }

    if (intro == 0x50u || intro == 0x5Fu || intro == 0x5Eu ||
        intro == 0x58u)  // DCS / APC / PM / SOS: ST terminated
    {
        ++i;
        while (i < n)
        {
            const auto b = static_cast<unsigned char>(s[i]);
            if (b == 0x9Cu)  // 8-bit ST
            {
                return i + 1;
            }
            if (b == 0x1Bu && i + 1 < n &&
                static_cast<unsigned char>(s[i + 1]) == 0x5Cu)  // ESC \ ST
            {
                return i + 2;
            }
            ++i;
        }
        return i;
    }

    // Short escape: optional intermediates then one final byte.
    while (i < n)
    {
        const auto b = static_cast<unsigned char>(s[i]);
        if (b >= 0x20u && b <= 0x2Fu)
        {
            ++i;
            continue;
        }
        break;
    }
    if (i < n)
    {
        const auto b = static_cast<unsigned char>(s[i]);
        if (b >= 0x30u && b <= 0x7Eu)
        {
            ++i;
        }
    }
    return i;
}

}  // namespace

std::size_t display_width(const std::string& utf8)
{
    std::size_t width = 0;
    std::size_t i = 0;
    while (i < utf8.size())
    {
        std::uint32_t cp = 0;
        std::size_t len = 0;
        decode_utf8(utf8, i, cp, len);
        width += codepoint_width(cp);
        i += len;
    }
    return width;
}

std::string truncate_to_width(const std::string& utf8, std::size_t width)
{
    if (width == 0)
    {
        return utf8.empty() ? utf8 : std::string();
    }

    // Phase 1: does the whole string fit within width? Stop as soon as the
    // budget is exceeded so a huge field is not walked in full (O(width)).
    {
        std::size_t w = 0;
        std::size_t i = 0;
        bool fits = true;
        while (i < utf8.size())
        {
            std::uint32_t cp = 0;
            std::size_t len = 0;
            decode_utf8(utf8, i, cp, len);
            w += codepoint_width(cp);
            if (w > width)
            {
                fits = false;
                break;
            }
            i += len;
        }
        if (fits)
        {
            return utf8;
        }
    }

    // Phase 2: keep whole code points up to (width - ellipsis) columns, then
    // append the ellipsis (shrunk to fit a very small width).
    const std::size_t ellipsis = width < 3 ? width : 3;
    const std::size_t budget = width - ellipsis;
    std::string out;
    std::size_t w = 0;
    std::size_t i = 0;
    while (i < utf8.size())
    {
        std::uint32_t cp = 0;
        std::size_t len = 0;
        decode_utf8(utf8, i, cp, len);
        const std::size_t cw = codepoint_width(cp);
        if (w + cw > budget)
        {
            break;
        }
        out.append(utf8, i, len);
        w += cw;
        i += len;
    }
    out.append(ellipsis, '.');
    return out;
}

std::string pad_to_width(const std::string& utf8,
                         std::size_t width,
                         bool right_align)
{
    const std::size_t have = display_width(utf8);
    if (have >= width)
    {
        return utf8;
    }
    const std::string pad(width - have, ' ');
    return right_align ? pad + utf8 : utf8 + pad;
}

std::string sanitize_for_terminal(std::string_view in, std::size_t max_bytes)
{
    std::string out;
    out.reserve(in.size() < max_bytes ? in.size() : max_bytes);

    const std::size_t n = in.size();
    std::size_t i = 0;
    while (i < n)
    {
        const auto byte = static_cast<unsigned char>(in[i]);

        if (byte == 0x1Bu)  // ESC and the whole sequence it introduces
        {
            i = skip_escape_sequence(in, i);
            continue;
        }
        if (byte == 0x7Fu)  // DEL
        {
            ++i;
            continue;
        }
        if (byte < 0x20u)  // remaining C0 controls -> single space
        {
            if (out.size() + 1 > max_bytes)
            {
                break;
            }
            out.push_back(' ');
            ++i;
            continue;
        }
        if (byte >= 0x80u && byte <= 0x9Fu)  // raw C1 byte (invalid lead)
        {
            ++i;
            continue;
        }

        std::uint32_t cp = 0;
        std::size_t len = 0;
        decode_utf8(in, i, cp, len);

        if (cp == k_replacement && len == 1)  // invalid UTF-8 byte
        {
            if (out.size() + 3 > max_bytes)
            {
                break;
            }
            append_utf8(k_replacement, out);
            ++i;
            continue;
        }

        if (is_forbidden_codepoint(cp))  // C1 / bidi / zero-width code points
        {
            i += len;
            continue;
        }

        if (out.size() + len > max_bytes)  // never split a sequence at the cap
        {
            break;
        }
        out.append(in.data() + i, len);
        i += len;
    }

    return out;
}

}  // namespace url_shortener::tui
