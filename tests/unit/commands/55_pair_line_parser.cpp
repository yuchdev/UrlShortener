#define BOOST_TEST_MODULE CommandsPairLineParserTest
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/pair_line_parser.hpp"

using url_shortener::commands::ImportErrorList;
using url_shortener::commands::kMaxImportLineBytes;
using url_shortener::commands::kMaxReportedImportErrors;
using url_shortener::commands::PairFormat;
using url_shortener::commands::PairLineParser;
using url_shortener::commands::ParsedRow;
using url_shortener::commands::RowKind;

namespace
{

// Convenience: parse a single CSV line through a fresh parser (no prior
// header state). line_number defaults to a non-first line so the header path
// is not triggered for data rows.
ParsedRow csv_line(std::string_view text, std::size_t line_number = 2)
{
    PairLineParser parser(PairFormat::csv);
    return parser.parse(text, line_number);
}

ParsedRow ndjson_line(std::string_view text, std::size_t line_number = 1)
{
    PairLineParser parser(PairFormat::ndjson);
    return parser.parse(text, line_number);
}

}  // namespace

// ---------------------------------------------------------------------------
// CSV
// ---------------------------------------------------------------------------

// Type: unit (pure parser).
// Scenario: a plain two-column row yields a pair with both fields.
// Boundaries: exactly two fields, slug present and non-empty.
// On failure first check: did tokenize_csv change field splitting or trimming?
BOOST_AUTO_TEST_CASE(csv_plain_row_is_pair)
{
    const ParsedRow row = csv_line("https://a.example/x,abc");
    BOOST_TEST((row.kind == RowKind::pair));
    BOOST_TEST(row.full_url == "https://a.example/x");
    BOOST_REQUIRE(row.slug.has_value());
    BOOST_TEST(row.slug.value() == "abc");
}

// Type: unit.
// Scenario: a trailing comma with an empty second column means "generate a
// slug" -> slug is nullopt, not an empty string.
// Boundaries: field 2 blank.
// On failure first check: did the blank-field -> nullopt mapping break?
BOOST_AUTO_TEST_CASE(csv_blank_slug_is_nullopt)
{
    const ParsedRow row = csv_line("https://a.example/x,");
    BOOST_TEST((row.kind == RowKind::pair));
    BOOST_TEST(row.full_url == "https://a.example/x");
    BOOST_TEST(!row.slug.has_value());
}

// Type: unit.
// Scenario: a one-column row (no comma) is a valid pair with slug nullopt.
// Boundaries: single field.
// On failure first check: did one-column handling regress to an error?
BOOST_AUTO_TEST_CASE(csv_one_column_row_is_pair)
{
    const ParsedRow row = csv_line("https://a.example/x");
    BOOST_TEST((row.kind == RowKind::pair));
    BOOST_TEST(row.full_url == "https://a.example/x");
    BOOST_TEST(!row.slug.has_value());
}

// Type: unit.
// Scenario: the header line is skipped only when it is the first non-blank
// line, in any ASCII case and for both header spellings.
// Boundaries: first line; mixed case; "full_url" and "full_url,slug".
// On failure first check: is_csv_header case-folding or first-line gating.
BOOST_AUTO_TEST_CASE(csv_header_skipped_only_when_first)
{
    {
        PairLineParser parser(PairFormat::csv);
        BOOST_TEST((parser.parse("full_url,slug", 1).kind == RowKind::skipped));
    }
    {
        PairLineParser parser(PairFormat::csv);
        BOOST_TEST((parser.parse("FULL_URL,SLUG", 1).kind == RowKind::skipped));
    }
    {
        PairLineParser parser(PairFormat::csv);
        BOOST_TEST((parser.parse("Full_Url", 1).kind == RowKind::skipped));
    }
    {
        // Blank lines before the header do not consume the first-line slot.
        PairLineParser parser(PairFormat::csv);
        BOOST_TEST((parser.parse("   ", 1).kind == RowKind::skipped));
        BOOST_TEST((parser.parse("full_url,slug", 2).kind == RowKind::skipped));
    }
}

// Type: unit.
// Scenario: a header-looking line appearing after the first data line is parsed
// as data, and since "full_url,slug" has two non-empty columns it is a pair.
// Boundaries: second non-blank line.
// On failure first check: did m_seen_first_data_line stop latching?
BOOST_AUTO_TEST_CASE(csv_header_looking_line_later_is_data)
{
    PairLineParser parser(PairFormat::csv);
    BOOST_TEST(
        (parser.parse("https://a.example/x,abc", 1).kind == RowKind::pair));
    const ParsedRow later = parser.parse("full_url,slug", 2);
    BOOST_TEST((later.kind == RowKind::pair));
    BOOST_TEST(later.full_url == "full_url");
    BOOST_REQUIRE(later.slug.has_value());
    BOOST_TEST(later.slug.value() == "slug");
}

// Type: unit.
// Scenario: a quoted field containing a comma keeps the comma as data.
// Boundaries: quote-delimited field with embedded comma.
// On failure first check: did the quoted-state comma handling regress?
BOOST_AUTO_TEST_CASE(csv_quoted_field_with_comma)
{
    const ParsedRow row = csv_line("\"https://a.example/x,y\",abc");
    BOOST_TEST((row.kind == RowKind::pair));
    BOOST_TEST(row.full_url == "https://a.example/x,y");
    BOOST_REQUIRE(row.slug.has_value());
    BOOST_TEST(row.slug.value() == "abc");
}

// Type: unit.
// Scenario: a "" inside a quoted field decodes to a single literal quote.
// Boundaries: escaped quote at the field interior.
// On failure first check: the "" -> " un-escaping in the quoted state.
BOOST_AUTO_TEST_CASE(csv_escaped_quote)
{
    const ParsedRow row = csv_line("\"a\"\"b\",s");
    BOOST_TEST((row.kind == RowKind::pair));
    BOOST_TEST(row.full_url == "a\"b");
    BOOST_REQUIRE(row.slug.has_value());
    BOOST_TEST(row.slug.value() == "s");
}

// Type: unit (invalid input).
// Scenario: an unterminated quote is an error; also a quote mid unquoted field.
// Boundaries: no closing quote; stray quote.
// On failure first check: quoted end-state and unquoted stray-quote branches.
BOOST_AUTO_TEST_CASE(csv_quote_errors)
{
    {
        const ParsedRow row = csv_line("\"abc");
        BOOST_TEST((row.kind == RowKind::error));
        BOOST_TEST(row.error_code == "invalid_field");
    }
    {
        const ParsedRow row = csv_line("ab\"c");
        BOOST_TEST((row.kind == RowKind::error));
        BOOST_TEST(row.error_code == "invalid_field");
    }
    {
        // Comma inside an unterminated quote is still unterminated.
        const ParsedRow row = csv_line("\"a,b");
        BOOST_TEST((row.kind == RowKind::error));
    }
}

// Type: unit (invalid input).
// Scenario: three columns exceed the two-column grammar -> error.
// Boundaries: field count 3.
// On failure first check: the fields.size() > 2 guard.
BOOST_AUTO_TEST_CASE(csv_three_columns_error)
{
    const ParsedRow row = csv_line("a,b,c");
    BOOST_TEST((row.kind == RowKind::error));
    BOOST_TEST(row.error_code == "invalid_field");
}

// Type: unit (invalid input).
// Scenario: an empty first column is an error (URL column is required).
// Boundaries: empty field 0 via leading comma and via empty line-with-comma.
// On failure first check: the fields[0].empty() guard.
BOOST_AUTO_TEST_CASE(csv_empty_url_error)
{
    {
        const ParsedRow row = csv_line(",slug");
        BOOST_TEST((row.kind == RowKind::error));
        BOOST_TEST(row.error_code == "invalid_field");
    }
    {
        const ParsedRow row = csv_line("   ,slug");
        BOOST_TEST((row.kind == RowKind::error));
    }
}

// Type: unit.
// Scenario: a leading UTF-8 BOM on line 1 and a trailing CR are stripped before
// parsing, so a CRLF-with-BOM first row parses like a plain row.
// Boundaries: line_number == 1 for BOM; trailing \r on any line.
// On failure first check: the BOM prefix and CR suffix stripping in parse().
BOOST_AUTO_TEST_CASE(csv_bom_and_crlf_stripped)
{
    {
        PairLineParser parser(PairFormat::csv);
        const ParsedRow row = parser.parse(
            "\xEF\xBB\xBF"
            "https://a.example/x,abc\r",
            1);
        BOOST_TEST((row.kind == RowKind::pair));
        BOOST_TEST(row.full_url == "https://a.example/x");
        BOOST_REQUIRE(row.slug.has_value());
        BOOST_TEST(row.slug.value() == "abc");
    }
    {
        // A BOM on a non-first line is NOT stripped and becomes field data.
        const ParsedRow row = csv_line(
            "\xEF\xBB\xBF"
            "u",
            2);
        BOOST_TEST((row.kind == RowKind::pair));
        BOOST_TEST(row.full_url == "\xEF\xBB\xBF" "u");
    }
}

// Type: unit.
// Scenario: a whitespace-only line is skipped, not an empty-URL error.
// Boundaries: spaces and tabs only.
// On failure first check: the trim_view blank guard runs before tokenizing.
BOOST_AUTO_TEST_CASE(csv_whitespace_only_skipped)
{
    const ParsedRow row = csv_line("  \t  ");
    BOOST_TEST((row.kind == RowKind::skipped));
}

// Type: unit (boundary).
// Scenario: a line exactly kMaxImportLineBytes long is accepted by the parser
// (the byte cap is the reader's job, not the parser's).
// Boundaries: line length == kMaxImportLineBytes.
// On failure first check: did the parser start rejecting long lines itself?
BOOST_AUTO_TEST_CASE(csv_very_long_line_accepted)
{
    std::string line = "https://a.example/";
    line.append(kMaxImportLineBytes - line.size(), 'a');
    BOOST_TEST(line.size() == kMaxImportLineBytes);
    const ParsedRow row = csv_line(line);
    BOOST_TEST((row.kind == RowKind::pair));
    BOOST_TEST(row.full_url.size() == kMaxImportLineBytes);
}

// ---------------------------------------------------------------------------
// NDJSON
// ---------------------------------------------------------------------------

// Type: unit.
// Scenario: a single well-formed object is a pair whose args_json round-trips
// the exact object text.
// Boundaries: one object, no trailing data.
// On failure first check: find_object_end balance and the args_json substr.
BOOST_AUTO_TEST_CASE(ndjson_valid_object_is_pair)
{
    const ParsedRow row = ndjson_line("{\"full_url\":\"https://a/x\"}");
    BOOST_TEST((row.kind == RowKind::pair));
    BOOST_TEST(row.args_json == "{\"full_url\":\"https://a/x\"}");
    BOOST_TEST(row.full_url.empty());
    BOOST_TEST(!row.slug.has_value());
}

// Type: unit.
// Scenario: nested braces and a closing brace inside a string literal do not
// end the object early; the whole object is one pair.
// Boundaries: '}' inside a string; a nested object.
// On failure first check: the in_string / escape handling in find_object_end.
BOOST_AUTO_TEST_CASE(ndjson_nested_and_brace_in_string)
{
    {
        const ParsedRow row = ndjson_line("{\"a\":\"}\"}");
        BOOST_TEST((row.kind == RowKind::pair));
        BOOST_TEST(row.args_json == "{\"a\":\"}\"}");
    }
    {
        const ParsedRow row = ndjson_line("{\"a\":{\"b\":1}}");
        BOOST_TEST((row.kind == RowKind::pair));
        BOOST_TEST(row.args_json == "{\"a\":{\"b\":1}}");
    }
    {
        // An escaped quote inside the string does not end the string.
        const ParsedRow row = ndjson_line("{\"a\":\"x\\\"}\"}");
        BOOST_TEST((row.kind == RowKind::pair));
    }
}

// Type: unit (invalid input).
// Scenario: two objects on one line, trailing garbage, an array line, and a
// truncated object are all errors.
// Boundaries: }{ boundary; non-{ first char; unterminated object.
// On failure first check: trailing-non-whitespace and npos branches of parse.
BOOST_AUTO_TEST_CASE(ndjson_structural_errors)
{
    for (const std::string_view bad : {std::string_view("{\"a\":1}{\"b\":2}"),
                                       std::string_view("{\"a\":1} x"),
                                       std::string_view("[1,2]"),
                                       std::string_view("{\"a\":1")})
    {
        const ParsedRow row = ndjson_line(bad);
        BOOST_TEST((row.kind == RowKind::error));
        BOOST_TEST(row.error_code == "invalid_field");
    }
}

// Type: unit.
// Scenario: a blank NDJSON line is skipped; surrounding whitespace is trimmed
// around a valid object.
// Boundaries: empty/whitespace; leading/trailing spaces.
// On failure first check: the trim_view blank guard and trimmed substr.
BOOST_AUTO_TEST_CASE(ndjson_blank_and_trim)
{
    BOOST_TEST((ndjson_line("   ").kind == RowKind::skipped));
    BOOST_TEST((ndjson_line("").kind == RowKind::skipped));
    const ParsedRow row = ndjson_line("   {\"a\":1}   ");
    BOOST_TEST((row.kind == RowKind::pair));
    BOOST_TEST(row.args_json == "{\"a\":1}");
}

// ---------------------------------------------------------------------------
// ImportErrorList
// ---------------------------------------------------------------------------

// Type: unit (boundary).
// Scenario: the error cap retains the first kMaxReportedImportErrors entries
// and counts the rest as omitted. Boundaries: 0, 100, 101, 1000 adds -> sizes
// 0/100/100/100, omitted 0/0/1/900. On failure first check: the m_items.size()
// < cap guard and omitted counter.
BOOST_AUTO_TEST_CASE(error_list_cap_boundaries)
{
    const std::array<std::size_t, 4> adds = {0, 100, 101, 1000};
    const std::array<std::size_t, 4> expected_size = {0,
                                                      kMaxReportedImportErrors,
                                                      kMaxReportedImportErrors,
                                                      kMaxReportedImportErrors};
    const std::array<std::size_t, 4> expected_omitted = {0, 0, 1, 900};

    for (std::size_t c = 0; c < adds.size(); ++c) {
        ImportErrorList errors;
        for (std::size_t i = 0; i < adds[c]; ++i) {
            errors.add(i + 1, "invalid_field");
        }
        BOOST_TEST(errors.items().size() == expected_size[c]);
        BOOST_TEST(errors.omitted() == expected_omitted[c]);
    }
}

// Type: unit.
// Scenario: to_json emits a valid JSON array in insertion order and escapes the
// code string; an empty list is "[]".
// Boundaries: empty list; a code containing a quote and backslash.
// On failure first check: the to_json join and jsonEscape call.
BOOST_AUTO_TEST_CASE(error_list_to_json)
{
    {
        ImportErrorList errors;
        BOOST_TEST(errors.to_json() == "[]");
    }
    {
        ImportErrorList errors;
        errors.add(7, "invalid_field");
        errors.add(42, "invalid_field");
        BOOST_TEST(errors.to_json() ==
                   "[{\"line\":7,\"code\":\"invalid_field\"},"
                   "{\"line\":42,\"code\":\"invalid_field\"}]");
    }
    {
        ImportErrorList errors;
        errors.add(1, "a\"b\\c");
        BOOST_TEST(errors.to_json()
                   == "[{\"line\":1,\"code\":\"a\\\"b\\\\c\"}]");
    }
}

// ---------------------------------------------------------------------------
// Fuzz-style robustness (success criterion 2 and threat model T-97)
// ---------------------------------------------------------------------------

// Type: unit (fuzz / invalid input).
// Scenario: a curated table of malformed lines never throws and never returns
// RowKind::pair in its target format (success criterion 2, >= 30 cases).
// Boundaries: CSV quote/column/empty errors; NDJSON structural errors; NUL,
// CR/LF variants, invalid UTF-8, lone BOM on a non-first line.
// On failure first check: did any branch of parse() throw or misclassify?
BOOST_AUTO_TEST_CASE(fuzz_malformed_lines_never_pair)
{
    // CSV: each of these is malformed for CSV (never a pair on a data line).
    const std::array<std::string_view, 16> csv_bad = {
        std::string_view(",only-slug"),
        std::string_view("   ,x"),
        std::string_view("a,b,c"),
        std::string_view("a,b,c,d"),
        std::string_view("\"unterminated"),
        std::string_view("\"a,b"),
        std::string_view("mid\"quote"),
        std::string_view("\"closed\" trailing"),
        std::string_view("\"a\"x,b"),
        std::string_view(","),
        std::string_view(",,"),
        std::string_view("\"\",slug"),
        std::string_view("  ,  "),
        std::string_view("\"a\0", 3),  // embedded NUL inside unterminated quote
        std::string_view("\"\xFF\xFE"),  // invalid UTF-8, unterminated quote
        std::string_view("x,y,\xFF"),  // 3 cols + invalid UTF-8
    };
    for (const std::string_view bad : csv_bad) {
        const ParsedRow row = csv_line(bad);
        BOOST_TEST((row.kind != RowKind::pair));
    }

    // NDJSON: each is malformed for NDJSON.
    const std::array<std::string_view, 16> ndjson_bad = {
        std::string_view("not json"),
        std::string_view("[1,2,3]"),
        std::string_view("{\"a\":1"),
        std::string_view("{\"a\":1}{\"b\":2}"),
        std::string_view("{\"a\":1} trailing"),
        std::string_view("{\"a\":\"unterminated}"),
        std::string_view("123"),
        std::string_view("true"),
        std::string_view("null"),
        std::string_view("\"string\""),
        std::string_view("}"),
        std::string_view("{{{"),
        std::string_view("{\"a\":1}\t\t x"),
        std::string_view("{\"a\":\0", 6),  // embedded NUL, object never closes
        std::string_view("{\xFF\xFE"),
        std::string_view(
            "\xEF\xBB\xBF{\"a\":1}"),  // BOM not stripped on line 2
    };
    for (const std::string_view bad : ndjson_bad) {
        const ParsedRow row = ndjson_line(bad, 2);
        BOOST_TEST((row.kind != RowKind::pair));
    }
}

// Type: unit (fuzz / resource bound, threat model T-97).
// Scenario: arbitrary byte lines (random bytes, NUL, invalid UTF-8, CR/LF, BOM,
// very long) never throw and parse in bounded memory (output no larger than the
// input line) regardless of content or format.
// Boundaries: deterministic LCG byte stream; lengths up to kMaxImportLineBytes.
// On failure first check: an unbounded accumulation in a tokenizer state
// branch.
BOOST_AUTO_TEST_CASE(fuzz_random_bytes_no_throw_bounded)
{
    std::uint64_t state = 0x9e3779b97f4a7c15ULL;
    auto next_byte = [&state]() -> char
    {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<char>((state >> 33) & 0xFFU);
    };

    for (int iter = 0; iter < 400; ++iter) {
        const std::size_t len = static_cast<std::size_t>(iter) * 41U % 20000U;
        std::string line;
        line.reserve(len);
        for (std::size_t i = 0; i < len; ++i) {
            line.push_back(next_byte());
        }

        PairLineParser csv(PairFormat::csv);
        const ParsedRow a = csv.parse(line, static_cast<std::size_t>(iter) + 1);
        // Output never exceeds input: bounded memory, no amplification.
        BOOST_TEST(a.full_url.size() <= line.size());
        BOOST_TEST(a.args_json.size() <= line.size());

        PairLineParser nd(PairFormat::ndjson);
        const ParsedRow b = nd.parse(line, static_cast<std::size_t>(iter) + 1);
        BOOST_TEST(b.args_json.size() <= line.size());
        BOOST_TEST(b.full_url.empty());
    }
}

// Type: unit (defensive resource bound, threat model T-97; subtask 07
// carry-forward).
// Scenario: a line longer than kMaxImportLineBytes is rejected as an error row
// by the parser itself (independent of the reader's cap), with a value-free
// code and no field populated.
// Boundaries: exactly kMaxImportLineBytes is still parsed; +1 is an error.
// On failure first check: did the defensive length guard at the top of parse()
// get removed or compare with the wrong operator?
BOOST_AUTO_TEST_CASE(line_over_cap_is_error_row)
{
    const std::string at_cap(kMaxImportLineBytes, 'a');
    const ParsedRow ok = csv_line(at_cap);
    BOOST_TEST((ok.kind == RowKind::pair));
    BOOST_TEST(ok.full_url == at_cap);

    const std::string over_cap(kMaxImportLineBytes + 1, 'a');
    const ParsedRow csv_row = csv_line(over_cap);
    BOOST_TEST((csv_row.kind == RowKind::error));
    BOOST_TEST(csv_row.error_code == "invalid_field");
    BOOST_TEST(csv_row.full_url.empty());

    const std::string over_json = "{" + std::string(kMaxImportLineBytes, 'a');
    const ParsedRow nd_row = ndjson_line(over_json);
    BOOST_TEST((nd_row.kind == RowKind::error));
    BOOST_TEST(nd_row.error_code == "invalid_field");
    BOOST_TEST(nd_row.args_json.empty());
}
