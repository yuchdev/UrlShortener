#include "url_shortener/commands/ndjson_line_reader.hpp"

#include <cstddef>
#include <string>

#include "url_shortener/commands/pair_file_access.hpp"

namespace url_shortener::commands
{

namespace
{

/**
 * @brief Strips a leading UTF-8 byte-order mark (`EF BB BF`) from @p line.
 *
 * Applied only to the first physical line of a stream. A no-op when the line is
 * shorter than three bytes or does not begin with the BOM sequence.
 *
 * @param line The line to adjust in place.
 */
void strip_bom(std::string& line)
{
    if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF
        && static_cast<unsigned char>(line[1]) == 0xBB
        && static_cast<unsigned char>(line[2]) == 0xBF) {
        line.erase(0, 3);
    }
}

/**
 * @brief Whether @p line contains a NUL byte.
 *
 * A NUL means the line is rejected as @ref read_result::invalid and never handed
 * to the JSON parser (framing table, threat model T-104).
 *
 * @param line The candidate content line.
 * @return `true` if any byte is `\0`.
 */
bool has_nul(const std::string& line)
{
    return line.find('\0') != std::string::npos;
}

/**
 * @brief Whether @p line is empty or made up entirely of ASCII whitespace.
 *
 * Such lines are skipped silently with no response. The terminating `\n` and any
 * preceding `\r` are already stripped by @ref read_bounded_line; a line of only
 * carriage returns (e.g. `"\r\r"`) therefore reads as whitespace-only here.
 *
 * @param line The candidate content line.
 * @return `true` if @p line holds no non-whitespace byte.
 */
bool is_blank(const std::string& line)
{
    for (const char ch : line) {
        switch (ch) {
            case ' ':
            case '\t':
            case '\r':
            case '\n':
            case '\v':
            case '\f':
                break;
            default:
                return false;
        }
    }
    return true;
}

}  // namespace

NdjsonLineReader::NdjsonLineReader(std::istream& in, std::size_t max_line_bytes)
    : m_in(in)
    , m_max(max_line_bytes)
{
}

read_result NdjsonLineReader::next(std::string& line)
{
    for (;;) {
        const line_status status = read_bounded_line(m_in, line, m_max);
        switch (status) {
            case line_status::eof:
                return read_result::eof;
            case line_status::io_error:
                return read_result::io_error;
            case line_status::too_long:
                ++m_line_number;
                m_first = false;
                line.clear();  // drop the over-long line whole (T-103)
                return read_result::too_long;
            case line_status::line:
                ++m_line_number;
                if (m_first) {
                    strip_bom(line);
                    m_first = false;
                }
                if (has_nul(line)) {
                    line.clear();
                    return read_result::invalid;
                }
                if (is_blank(line)) {
                    continue;  // skip blank lines silently
                }
                return read_result::line;
        }
    }
}

std::size_t NdjsonLineReader::line_number() const noexcept
{
    return m_line_number;
}

}  // namespace url_shortener::commands
