#pragma once

#include <cstddef>
#include <istream>
#include <string>

namespace url_shortener::commands
{

/**
 * @brief Verdict of one @ref NdjsonLineReader::next call.
 *
 * Every byte-stream outcome maps to exactly one of these values, so the session
 * loop (subtask 14) never guesses: it either gets a content line or an explicit
 * reason it got none. Only @ref read_result::line fills the out-parameter.
 */
enum class read_result
{
    ///< A non-blank content line was produced in the out-parameter, with its
    ///< terminator, any leading UTF-8 BOM (first physical line only) and any
    ///< surrounding blank lines already removed.
    line,

    ///< The physical line exceeded `max_line_bytes`; it was consumed to its end
    ///< and dropped whole (not stored, not parsed), keeping memory bounded
    ///< (threat model T-103). The out-parameter is left empty.
    too_long,

    ///< The line contained a NUL byte and is refused before any JSON parsing
    ///< (framing table, threat model T-104); the physical line was consumed. The
    ///< out-parameter is left empty.
    invalid,

    ///< The stream was exhausted with no pending partial line. The
    ///< out-parameter is left empty.
    eof,

    ///< The underlying stream reported a hard read error (`std::ios::badbit`)
    ///< while not at end of stream. The out-parameter is left empty.
    io_error
};

/**
 * @brief Turns a byte stream into NDJSON request lines with bounded memory.
 *
 * Wraps the shared @ref read_bounded_line primitive (subtask 07) with the
 * stream-oriented framing the NDJSON wire requires and nothing more: a hard
 * per-line size cap, tolerant line endings (`\n`, a preceding `\r` stripped, a
 * final line without a terminator), a first-line UTF-8 BOM rule, silent skipping
 * of blank/whitespace-only lines, and a NUL-byte rejection. It performs no JSON
 * parsing; the envelope parser downstream validates content.
 *
 * The framing is deliberately identical for every NDJSON transport (stdio now,
 * IPC later, constraint C2): the reader is not parameterized per transport.
 * Peak memory is bounded by `max_line_bytes` plus the stream's own buffer
 * regardless of input length - an arbitrarily long physical line is consumed and
 * dropped, never buffered (threat model T-103).
 *
 * Not thread-safe: a single session owns one reader and calls @ref next from one
 * thread.
 */
class NdjsonLineReader
{
public:
    /**
     * @brief Binds a reader to a byte stream and a per-line byte cap.
     *
     * @param in The source stream, read forward one physical line per @ref next
     * call. Treated as untrusted and never logged. The reference is captured and
     * must outlive this reader (lifetime owned by the caller / session).
     * @param max_line_bytes The maximum number of bytes a single logical line
     * may contribute before it is reported as @ref read_result::too_long;
     * callers pass `ServerConfig::max_request_body_bytes` (default 65536).
     */
    NdjsonLineReader(std::istream& in, std::size_t max_line_bytes);

    /**
     * @brief Reads the next content line, skipping blank lines internally.
     *
     * Loops over blank and whitespace-only physical lines (counting each toward
     * @ref line_number) so a caller only ever sees a content line or a verdict.
     * On the first physical line a leading UTF-8 BOM is stripped; on later lines
     * it is kept. A line containing a NUL byte is reported @ref
     * read_result::invalid and never returned as content.
     *
     * @param line Cleared on entry; filled with the content line's bytes only
     * when the return value is @ref read_result::line. Left empty for every other
     * result.
     * @return @ref read_result::line on a content line; @ref
     * read_result::too_long when the physical line exceeded `max_line_bytes`
     * (consumed and dropped); @ref read_result::invalid when the line held a NUL
     * byte (consumed); @ref read_result::eof when the stream is exhausted; @ref
     * read_result::io_error on a hard stream error that is not end of stream.
     */
    read_result next(std::string& line);

    /**
     * @brief The number of physical lines consumed from the stream so far.
     *
     * Counts every physical line, including blank lines skipped by @ref next and
     * lines reported as @ref read_result::too_long or @ref read_result::invalid,
     * so a diagnostic can cite the real 1-based position of a line.
     *
     * @return `0` before the first read; otherwise the 1-based index of the most
     * recently consumed physical line.
     */
    std::size_t line_number() const noexcept;

private:
    ///< The untrusted source stream; owned by the caller, not this reader.
    std::istream& m_in;

    ///< Per-line byte cap; a longer logical line is dropped as `too_long`.
    std::size_t m_max;

    ///< Count of physical lines consumed; 1-based after the first read.
    std::size_t m_line_number = 0;

    ///< `true` until the first physical line is consumed; gates BOM stripping.
    bool m_first = true;
};

}  // namespace url_shortener::commands
