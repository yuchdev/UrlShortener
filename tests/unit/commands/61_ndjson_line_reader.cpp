#define BOOST_TEST_MODULE CommandsNdjsonLineReaderTest
#include <cstddef>
#include <ios>
#include <sstream>
#include <streambuf>
#include <string>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/ndjson_line_reader.hpp"

using url_shortener::commands::NdjsonLineReader;
using url_shortener::commands::read_result;

namespace
{

/// Builds a reader over a fresh stream holding @p text; the stream is kept alive
/// by the returned pair so callers bind a reference that outlives their reads.
struct reader_over
{
    std::istringstream stream;
    NdjsonLineReader reader;

    explicit reader_over(std::string text, std::size_t max = 64)
        : stream(std::move(text))
        , reader(stream, max)
    {
    }
};

/// A streambuf whose first read throws, so the owning istream latches badbit
/// without reaching EOF - the precise condition the reader reports as io_error.
class throwing_streambuf : public std::streambuf
{
protected:
    int_type underflow() override
    {
        throw std::ios_base::failure("read failed");
    }
};

}  // namespace

// Type: unit (framing happy path).
// Scenario: LF-separated content lines are returned one per next() call with the
//   terminator stripped, then eof.
// Boundaries: distinguishes a content line from the trailing eof.
// On failure first check: did read_bounded_line change its terminator handling?

BOOST_AUTO_TEST_CASE(lf_separated_lines)
{
    reader_over in("alpha\nbeta\ngamma\n");
    std::string line;
    BOOST_TEST((in.reader.next(line) == read_result::line));
    BOOST_TEST(line == "alpha");
    BOOST_TEST((in.reader.next(line) == read_result::line));
    BOOST_TEST(line == "beta");
    BOOST_TEST((in.reader.next(line) == read_result::line));
    BOOST_TEST(line == "gamma");
    BOOST_TEST((in.reader.next(line) == read_result::eof));
    BOOST_TEST(in.reader.line_number() == 3U);
}

// Type: unit (CRLF and mixed endings).
// Scenario: a CR preceding each LF is stripped; LF-only and CRLF lines in one
//   stream read identically.
// Boundaries: a lone trailing CR on the final (newline-less) line is stripped
//   too, so that line is not seen as whitespace-only content.
// On failure first check: the CRLF branch of read_bounded_line.

BOOST_AUTO_TEST_CASE(crlf_and_mixed_endings)
{
    reader_over in("one\r\ntwo\nthree\r\n");
    std::string line;
    BOOST_TEST((in.reader.next(line) == read_result::line));
    BOOST_TEST(line == "one");
    BOOST_TEST((in.reader.next(line) == read_result::line));
    BOOST_TEST(line == "two");
    BOOST_TEST((in.reader.next(line) == read_result::line));
    BOOST_TEST(line == "three");
    BOOST_TEST((in.reader.next(line) == read_result::eof));
}

// Type: unit (final line without newline).
// Scenario: a stream whose last line lacks a terminator still yields that line.
// Boundaries: last-line-without-newline vs an empty stream.
// On failure first check: the no-trailing-newline branch of read_bounded_line.

BOOST_AUTO_TEST_CASE(last_line_without_newline)
{
    reader_over in("first\nlast-no-newline");
    std::string line;
    BOOST_TEST((in.reader.next(line) == read_result::line));
    BOOST_TEST(line == "first");
    BOOST_TEST((in.reader.next(line) == read_result::line));
    BOOST_TEST(line == "last-no-newline");
    BOOST_TEST((in.reader.next(line) == read_result::eof));
}

// Type: unit (empty input).
// Scenario: an empty stream reports eof immediately with no line consumed.
// Boundaries: eof with line_number 0.
// On failure first check: the saw_byte guard in read_bounded_line.

BOOST_AUTO_TEST_CASE(empty_input_is_eof)
{
    reader_over in("");
    std::string line;
    BOOST_TEST((in.reader.next(line) == read_result::eof));
    BOOST_TEST(line.empty());
    BOOST_TEST(in.reader.line_number() == 0U);
}

// Type: unit (blank-line skipping and physical counting).
// Scenario: a stream of only blank / whitespace-only lines is consumed silently
//   and reports eof, while line_number counts every skipped physical line.
// Boundaries: a line of only carriage returns counts as blank; next() never
//   returns an empty content line.
// On failure first check: is_blank's whitespace set and the skip loop.

BOOST_AUTO_TEST_CASE(only_blank_lines_eof_counts_physical)
{
    reader_over in("\n   \n\t\n\r\n\r\r\n");
    std::string line;
    BOOST_TEST((in.reader.next(line) == read_result::eof));
    BOOST_TEST(in.reader.line_number() == 5U);
}

// Type: unit (blank lines between content).
// Scenario: blank lines surrounding content are skipped; line_number reflects
//   the real physical position of the content line.
// Boundaries: a content line after two blanks reports line_number 3.
// On failure first check: whether skipped lines increment line_number.

BOOST_AUTO_TEST_CASE(whitespace_only_lines_skipped)
{
    reader_over in("\n  \npayload\n");
    std::string line;
    BOOST_TEST((in.reader.next(line) == read_result::line));
    BOOST_TEST(line == "payload");
    BOOST_TEST(in.reader.line_number() == 3U);
}

// Type: unit (exact-cap boundary).
// Scenario: a line of exactly max bytes is a valid line; max+1 bytes is
//   too_long, and the following line is still readable.
// Boundaries: max vs max+1.
// On failure first check: the `< max_line_bytes` vs overflow boundary.

BOOST_AUTO_TEST_CASE(cap_boundary_line_then_too_long_then_recover)
{
    const std::size_t max = 8;
    reader_over in(std::string(max, 'a') + "\n" + std::string(max + 1, 'b')
                       + "\nok\n",
                   max);
    std::string line;
    BOOST_TEST((in.reader.next(line) == read_result::line));
    BOOST_TEST(line == std::string(max, 'a'));
    BOOST_TEST((in.reader.next(line) == read_result::too_long));
    BOOST_TEST(line.empty());  // over-long line dropped whole (T-103/T-104)
    BOOST_TEST((in.reader.next(line) == read_result::line));
    BOOST_TEST(line == "ok");
    BOOST_TEST(in.reader.line_number() == 3U);
}

// Type: unit (memory bound, threat model T-103).
// Scenario: a 10 MiB single line with no newline is reported too_long while the
//   out-parameter's capacity stays bounded by the per-line cap.
// Boundaries: peak `line` allocation <= twice max regardless of input length.
// On failure first check: did next() start buffering the over-long remainder
//   instead of dropping it in read_bounded_line's overflow branch?

BOOST_AUTO_TEST_CASE(ten_mib_line_is_too_long_bounded_capacity)
{
    const std::size_t max = 4096;
    reader_over in(std::string(10u * 1024 * 1024, 'x'), max);
    std::string line;
    BOOST_TEST((in.reader.next(line) == read_result::too_long));
    BOOST_TEST(line.capacity() <= 2 * max);
    BOOST_TEST((in.reader.next(line) == read_result::eof));
}

// Type: unit (first-line BOM rule).
// Scenario: a UTF-8 BOM on the first line is stripped; a BOM on the second line
//   is kept as content bytes.
// Boundaries: first physical line vs later lines.
// On failure first check: the m_first gate around strip_bom.

BOOST_AUTO_TEST_CASE(bom_stripped_on_first_line_only)
{
    const std::string bom = "\xEF\xBB\xBF";
    reader_over in(bom + "first\n" + bom + "second\n");
    std::string line;
    BOOST_TEST((in.reader.next(line) == read_result::line));
    BOOST_TEST(line == "first");
    BOOST_TEST((in.reader.next(line) == read_result::line));
    BOOST_TEST(line == bom + "second");
}

// Type: unit (invalid-input: NUL byte).
// Scenario: a line containing a NUL byte is rejected as invalid and never
//   returned as content; the following line is read normally.
// Boundaries: invalid verdict vs a clean content line after it.
// On failure first check: has_nul and that line is cleared on invalid.

BOOST_AUTO_TEST_CASE(nul_byte_is_invalid_then_recover)
{
    reader_over in(std::string("ab\0cd", 5) + "\nclean\n");
    std::string line;
    BOOST_TEST((in.reader.next(line) == read_result::invalid));
    BOOST_TEST(line.empty());
    BOOST_TEST((in.reader.next(line) == read_result::line));
    BOOST_TEST(line == "clean");
    BOOST_TEST(in.reader.line_number() == 2U);
}

// Type: unit (io_error).
// Scenario: a stream whose read throws (badbit latched, not EOF) reports
//   io_error.
// Boundaries: io_error (bad) vs eof (clean end).
// On failure first check: read_bounded_line's in.bad() branch ordering before
//   the saw_byte check.

BOOST_AUTO_TEST_CASE(bad_stream_is_io_error)
{
    throwing_streambuf buf;
    std::istream stream(&buf);
    NdjsonLineReader reader(stream, 64);
    std::string line;
    BOOST_TEST((reader.next(line) == read_result::io_error));
    BOOST_TEST(line.empty());
}

// Type: unit (io_error via preset badbit).
// Scenario: a stream already in badbit state reports io_error, not eof.
// Boundaries: a non-empty buffer that cannot be read.
// On failure first check: whether badbit is checked before the no-byte eof path.

BOOST_AUTO_TEST_CASE(preset_badbit_is_io_error)
{
    reader_over in("data\n");
    in.stream.setstate(std::ios::badbit);
    std::string line;
    BOOST_TEST((in.reader.next(line) == read_result::io_error));
}

// Type: unit (line_number across skips and errors).
// Scenario: line_number tracks the true physical position across a blank line, a
//   too_long line, an invalid line, and content.
// Boundaries: every physical line increments the counter exactly once.
// On failure first check: the ++m_line_number placement in each next() branch.

BOOST_AUTO_TEST_CASE(line_number_spans_skips_and_errors)
{
    const std::size_t max = 4;
    reader_over in("\n" + std::string(max + 1, 'x') + "\n"
                       + std::string("a\0b", 3) + "\ndone\n",
                   max);
    std::string line;
    BOOST_TEST((in.reader.next(line) == read_result::too_long));
    BOOST_TEST(in.reader.line_number() == 2U);  // blank(1) + too_long(2)
    BOOST_TEST((in.reader.next(line) == read_result::invalid));
    BOOST_TEST(in.reader.line_number() == 3U);
    BOOST_TEST((in.reader.next(line) == read_result::line));
    BOOST_TEST(line == "done");
    BOOST_TEST(in.reader.line_number() == 4U);
}
