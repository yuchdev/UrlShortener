#pragma once

#include <optional>
#include <string>
#include <vector>

#include <url_shortener/tui/table_model.hpp>

namespace url_shortener::tui
{

/**
 * @file row_mapper.hpp
 * @brief Maps a command `result` payload into a @ref PageResult for the
 * @ref TableModel, using only the sanctioned JSON view helpers (contract C9).
 *
 * This is pure, FTXUI-free model code: it reads JSON through `json_view` (over
 * the one scanner) and performs no terminal sanitization (the render sinks do
 * that, T-110). It is the model layer's single conversion from a result
 * envelope's payload to table cells; screens supply the command-specific column
 * keys and the array field name.
 */

/**
 * @brief Builds a @ref PageResult from a command `result` payload.
 *
 * Reads @p items_field (e.g. `items` for `link.list`, `rows` for `db.rows`) as
 * a JSON array; for each element it produces one cell per @p column_keys and
 * keeps the element's exact JSON in @ref PageResult::raw_rows. A key may be a
 * dotted path (`stats.total_redirects`), which is resolved through nested
 * objects. A cell whose field is absent, null, or a container renders as `-`
 * (the D3 convention for empty values). @ref PageResult::total comes from the
 * payload's `total`, falling back to the number of elements when absent.
 *
 * At most 1000 elements are mapped into rows regardless of how many the server
 * returns (above the catalog's largest page, `db.rows` limit 500), bounding
 * per-call work if a backend over-returns (threat model T-121); @ref
 * PageResult::total still reflects the payload's reported count.
 *
 * @param result_json The command response's `result` payload (a JSON object).
 * @param column_keys The field key (or dotted path) each output column reads,
 * in column order.
 * @param items_field The payload field holding the row array.
 * @return A @ref PageResult on success (including an empty-but-well-formed
 * array, which yields no rows); `std::nullopt` when @p items_field is absent or
 * @p result_json is not a scannable object (malformed), so the caller can move
 * the model into @ref LoadState::error rather than showing a false-empty page.
 */
std::optional<PageResult> rows_from_result(
    const std::string& result_json,
    const std::vector<std::string>& column_keys,
    const std::string& items_field);

}  // namespace url_shortener::tui
