#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace url_shortener::commands
{

/**
 * @brief Wire format of a single `link.import`/`link.export` pair stream.
 *
 * Selects which per-line grammar @ref PairLineParser applies. The two formats
 * are the only ones supported in V1 (catalog section 3.3); other delimiters or
 * encodings are out of scope.
 */
enum class PairFormat
{
    ///< Comma-separated `full_url,slug` rows (RFC 4180 subset, line based);
    ///< the slug column may be blank or absent.
    csv,

    ///< Newline-delimited JSON: exactly one `link.create` args object per line.
    ndjson
};

/**
 * @brief Maximum byte length of a single import line.
 *
 * Not enforced by @ref PairLineParser itself (it accepts a line of any length
 * handed to it); the line reader of subtask 07 caps and truncates over-long
 * lines before they reach the parser (threat model T-97). Declared here so the
 * reader and the parser share one authoritative constant.
 */
inline constexpr std::size_t kMaxImportLineBytes = 16384;

/**
 * @brief Maximum number of data rows a single import job will process.
 *
 * Enforced by the import command of subtask 08, not by this parsing library;
 * declared here as the shared authoritative bound (threat model T-97).
 */
inline constexpr std::size_t kMaxImportRows = 100000;

/**
 * @brief Maximum number of per-line errors retained by @ref ImportErrorList.
 *
 * Errors beyond this cap are counted (see @ref ImportErrorList::omitted) but
 * not stored, bounding the memory an all-malformed import can consume.
 */
inline constexpr std::size_t kMaxReportedImportErrors = 100;

/**
 * @brief Classification of one parsed physical line.
 *
 * Returned in @ref ParsedRow::kind. A @ref PairLineParser call yields exactly
 * one of these for every physical line it is handed.
 */
enum class RowKind
{
    ///< A usable data row. For CSV, @ref ParsedRow::full_url (and optionally
    ///< @ref ParsedRow::slug) are set; for NDJSON, @ref ParsedRow::args_json
    ///< holds the unvalidated object text.
    pair,

    ///< The line carries no data and is ignored: a blank/whitespace-only line,
    ///< or the recognized CSV header on the first non-blank line.
    skipped,

    ///< The line is malformed for its format; @ref ParsedRow::error_code is
    ///< set and no field is trustworthy.
    error
};

/**
 * @brief Outcome of parsing one physical line of a pair stream.
 *
 * A value type with no ownership; which members are meaningful depends on
 * @ref kind (see each member). By design it never carries a decoded or
 * validated URL/slug: URL and slug validation is performed exactly once, later,
 * by `LinkCommandService::CreateLink` (plan C1), so this struct only transports
 * raw field text.
 */
struct ParsedRow
{
    ///< Classification of the line; determines which other members are set.
    RowKind kind = RowKind::skipped;

    ///< 1-based physical line number the row came from, copied from the
    ///< `line_number` argument of @ref PairLineParser::parse. Used only for
    ///< error reporting; never combined with line content.
    std::size_t line = 0;

    ///< CSV only: the trimmed first-column text, verbatim and unvalidated.
    ///< Empty for NDJSON rows and for non-`pair` rows.
    std::string full_url;

    ///< CSV only: the trimmed second-column text, or `std::nullopt` when the
    ///< column is blank or absent (meaning "generate a slug"). Always
    ///< `std::nullopt` for NDJSON rows and non-`pair` rows.
    std::optional<std::string> slug;

    ///< NDJSON only: the exact source text of the single JSON object, brace to
    ///< brace, left unvalidated for the shared `link.create` args decoder to
    ///< check. Empty for CSV rows and non-`pair` rows.
    std::string args_json;

    ///< Set to `"invalid_field"` when @ref kind is @ref RowKind::error; empty
    ///< otherwise. Carries a code only, never any offending content (plan C6).
    std::string error_code;
};

/**
 * @brief Stateful, line-oriented parser for one CSV or NDJSON pair stream.
 *
 * Pure parsing with no I/O: it reads neither files nor storage, performs no
 * URL/slug validation (plan C1), and never throws for any input. A single
 * instance parses one logical stream in order, because it carries one bit of
 * state - whether the first non-blank line has been seen - to recognize a CSV
 * header only in its leading position. Reuse one instance per import; do not
 * share an instance across streams or threads.
 *
 * Memory is bounded: each @ref parse call is a single pass over the supplied
 * line with allocations proportional to that line's length, no recursion, and
 * no decompression (threat model T-97). The byte cap on a line is the reader's
 * job (subtask 07), not this parser's.
 */
class PairLineParser
{
public:
    /**
     * @brief Constructs a parser for @p format.
     *
     * @param format The wire format every line handed to @ref parse is
     * interpreted as; fixed for the lifetime of this instance.
     */
    explicit PairLineParser(PairFormat format);

    /**
     * @brief Parses one physical line and classifies it.
     *
     * Stateful across calls only for CSV header detection: the recognized
     * header is skipped only when it is the first non-blank line; an identical
     * line appearing later is parsed as data (and typically becomes an error).
     * A leading UTF-8 BOM is stripped on line 1 and a single trailing `\r` is
     * stripped on every line, so CRLF streams parse like LF streams. Never
     * throws.
     *
     * @param line The raw bytes of one physical line, without its terminating
     * newline. Treated as untrusted; never logged. Any length is accepted (the
     * reader enforces @ref kMaxImportLineBytes).
     * @param line_number The 1-based physical line number, copied into
     * @ref ParsedRow::line and used only for error reporting.
     * @return A @ref ParsedRow whose @ref ParsedRow::kind is: @ref
     * RowKind::skipped for a blank/whitespace-only line or the leading CSV
     * header; @ref RowKind::pair for a usable row; or @ref RowKind::error with
     * @ref ParsedRow::error_code set to `"invalid_field"` for a malformed line
     * (CSV: a quote in the middle of an unquoted field, an unterminated quote,
     * more than two columns, or an empty first column; NDJSON: text that is not
     * exactly one `{...}` object with no trailing non-whitespace).
     */
    ParsedRow parse(std::string_view line, std::size_t line_number);

private:
    ///< The format fixed at construction; selects the per-line grammar.
    PairFormat m_format;

    ///< `true` once the first non-blank line has been seen, so the CSV header
    ///< is only ever recognized in its leading position. Unused for NDJSON.
    bool m_seen_first_data_line = false;
};

/**
 * @brief Append-only list of per-line import errors, capped for bounded memory.
 *
 * Retains the first @ref kMaxReportedImportErrors `(line, code)` pairs and
 * counts the rest (threat model T-97): an import whose every row is malformed
 * cannot grow this list without bound. Each entry is value-free beyond the line
 * number and a short code (plan C6); it never holds any offending content, so
 * @ref to_json is always safe to return to an untrusted caller.
 */
class ImportErrorList
{
public:
    /**
     * @brief Records one error for @p line, or counts it as omitted past the
     * cap.
     *
     * Keeps the entry only while fewer than @ref kMaxReportedImportErrors are
     * stored; otherwise discards @p code and increments @ref omitted. Never
     * throws beyond a potential allocation of the underlying vector.
     *
     * @param line The 1-based physical line number the error occurred on.
     * @param code A short, value-free error code (e.g. `"invalid_field"`);
     * moved into the list when retained.
     */
    void add(std::size_t line, std::string code);

    /**
     * @brief The retained `(line, code)` entries in insertion order.
     *
     * @return A reference to the stored entries, at most @ref
     * kMaxReportedImportErrors of them; valid while this list is alive and
     * unmodified.
     */
    const std::vector<std::pair<std::size_t, std::string>>&
    items() const noexcept;

    /**
     * @brief The number of errors dropped because the cap was already reached.
     *
     * @return Count of @ref add calls beyond @ref kMaxReportedImportErrors;
     * `0` when nothing was dropped.
     */
    std::size_t omitted() const noexcept;

    /**
     * @brief Serializes the retained entries as a JSON array.
     *
     * @return A JSON array `[{"line":n,"code":"..."},...]` in insertion order;
     * `"[]"` when empty. Codes are JSON-escaped. The omitted count is not
     * included; callers report it separately.
     */
    std::string to_json() const;

private:
    ///< Retained entries, capped at @ref kMaxReportedImportErrors.
    std::vector<std::pair<std::size_t, std::string>> m_items;

    ///< Count of errors dropped after the cap was reached.
    std::size_t m_omitted = 0;
};

}  // namespace url_shortener::commands
