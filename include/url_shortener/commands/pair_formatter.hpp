#pragma once

#include <string>

namespace url_shortener
{
namespace app
{

// Forward declaration: the pair formatters read an already-decoded link view by
// const reference and copy out only its public fields. The full definition
// lives in `app/link_command_service.hpp`, which transitively pulls in Beast via
// `core/utils.hpp`; that header is therefore included only by the formatter
// `.cpp`, never here (01.0/13 include-hygiene test).
struct LinkView;

}  // namespace app
}  // namespace url_shortener

namespace url_shortener::commands
{

/**
 * @brief The fixed CSV header line emitted once at the top of a CSV export.
 *
 * The companion of @ref format_csv_row: the two columns it names
 * (`full_url,slug`) are exactly the fields @ref format_csv_row emits, in the
 * same order, and the recognized header @ref PairLineParser skips on re-import
 * (subtask 06). Terminated by a single `\n`.
 *
 * @return The literal `"full_url,slug\n"`.
 */
std::string csv_header();

/**
 * @brief Serializes one link as a CSV `full_url,slug` row (RFC 4180 subset).
 *
 * The inverse of the CSV grammar @ref PairLineParser accepts (subtask 06), so an
 * exported CSV stream round-trips back through `link.import` unchanged (the
 * export contract of subtask 10). A field is double-quoted when it contains a
 * comma, a double quote, a CR or an LF, and every embedded double quote is
 * doubled (`"` -> `""`); any other byte is emitted verbatim, so the escaping is
 * safe for arbitrary bytes a legacy target URL may carry even though write-time
 * validation now rejects control bytes (25d66be). Cells are **not** neutralized
 * against spreadsheet formula injection and do not need to be (threat model
 * T-98): a `LinkView::target_url` is always an `http(s)://...` URL and a
 * `LinkView::slug` matches `^[A-Za-z0-9][A-Za-z0-9_-]{2,63}$`, so neither cell
 * can begin with `=`, `+`, `-`, `@`, a tab or a CR.
 *
 * @param link The decoded link view; only `target_url` and `slug` are read.
 * @return One CSV row terminated by a single `\n`.
 */
std::string format_csv_row(const app::LinkView& link);

/**
 * @brief Serializes one link as a single NDJSON `link.create` args object.
 *
 * Emits exactly the keys a `link.create` call accepts so the line re-imports
 * through the shared `link.create` schema and decoder unchanged (contract C1):
 * `target_url`, `slug`, `redirect_type`, `enabled`, `tags` and `metadata`
 * always, plus `expires_at` and `campaign` only when the link carries them.
 * Server-assigned fields (`id`, `short_url`, timestamps, computed `status`) are
 * never emitted - they are not valid `link.create` args and would be rejected on
 * re-import. All string values are escaped with the sanctioned `jsonString`
 * helper (contract C9), and `metadata` keys are emitted in sorted order for a
 * deterministic line.
 *
 * @param link The decoded link view to serialize.
 * @return One JSON object terminated by a single `\n`.
 */
std::string format_ndjson_row(const app::LinkView& link);

}  // namespace url_shortener::commands
