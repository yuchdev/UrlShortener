#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/table_model.hpp>

namespace url_shortener::tui
{

/**
 * @file tables_model.hpp
 * @brief The FTXUI-free model behind the Database tables browser (D6, subtask
 * 06.0/14): @ref TablesModel parses a `db.tables` result into the left-pane
 * table list, builds the `db.rows` request for the selected table, and derives
 * the right-pane @ref ColumnDef set from the selected table's column metadata.
 *
 * It belongs to `url_shortener_tui_model` (ADR 0003 rule 6): no FTXUI, no
 * colour, no threads. JSON is read only through the sanctioned `json_view`
 * helpers (contract C9); no string is sanitized here (the render sinks do that,
 * T-110). Every server-derived quantity is length-bounded (threat model T-121):
 * the table list is capped at @ref TablesModel::k_max_tables and each table's
 * column list at @ref TablesModel::k_max_columns.
 *
 * Secret safety is primarily server-side (milestone contract C6), but as
 * defence in depth @ref columns_for_selected refuses to surface any column
 * whose name looks like a secret (`hash`/`token`/`salt`/`secret`/`password`/
 * `dsn`, case-insensitive) with the single documented exception of
 * `client_id_hash` (implementation step 5).
 */

/**
 * @brief One column's metadata as reported by `db.tables` for a logical table.
 *
 * A pure value type. @ref sortable and @ref searchable come straight from the
 * `db.tables` column object and drive which affordances the screen offers
 * (`s sort` only on sortable columns, `/ search` only when some column is
 * searchable); they are not carried by the narrower `db.rows` column metadata.
 */
struct TableColumnInfo
{
    ///< Column name; also the `db.rows` row field key and the allowed `sort`
    ///< value when @ref sortable.
    std::string name;

    ///< Declared column type (e.g. `string`, `int`, `bool`, `timestamp`); used
    ///< to pick a sensible display width and alignment.
    std::string type;

    ///< `true` when `db.rows` may order by this column (the server's
    ///< whitelist).
    bool sortable = false;

    ///< `true` when this column participates in the `q` free-text filter.
    bool searchable = false;
};

/**
 * @brief One logical table as reported by `db.tables`.
 *
 * A pure value type. @ref rows is the server's row count for the table (shown
 * with thousands separators in the left pane); @ref writable decides whether an
 * insert affordance is offered; @ref columns is the per-column metadata used to
 * derive the right-pane @ref ColumnDef set.
 */
struct TableInfo
{
    ///< Logical table name (constrained to `[a-z][a-z0-9_]*` by the server's
    ///< allowlist); the `db.rows` `table` argument.
    std::string name;

    ///< Total row count from `db.tables`, for the left-pane count display.
    std::uint64_t rows = 0;

    ///< `true` when the table accepts inserts (only `links` in v1).
    bool writable = false;

    ///< The table's column metadata in server order (bounded by
    ///< @ref TablesModel::k_max_columns).
    std::vector<TableColumnInfo> columns;
};

/**
 * @brief Parses `db.tables`, tracks the selected table, and builds the
 * `db.rows` request and right-pane columns for it (D6, subtask 06.0/14).
 *
 * Pure and headless: every method is a deterministic function of the parsed
 * tables and the current selection, so the whole parsing / request-building /
 * column-derivation surface is unit-tested without a terminal (ADR 0003
 * rule 6). Single-threaded: created and mutated only on the UI thread.
 */
class TablesModel
{
public:
    /**
     * @brief Upper bound on tables materialized from one `db.tables` result, so
     * a backend that over-returns cannot inflate the left pane (T-121).
     */
    static constexpr std::size_t k_max_tables = 64;

    /**
     * @brief Upper bound on columns kept per table, so a wide table cannot
     * inflate the derived column set or per-frame layout work (T-121).
     */
    static constexpr std::size_t k_max_columns = 64;

    /**
     * @brief Default page size for the `db.rows` request (D6 "20 per page").
     */
    static constexpr std::size_t k_page_size = 20;

    /**
     * @brief Replaces the table list from a `db.tables` result payload.
     *
     * Reads the result's `tables` array (capped at @ref k_max_tables) and, for
     * each entry, its `name`, `rows`, `writable` and `columns` (each column's
     * `name`, `type`, `sortable`, `searchable`, capped at @ref k_max_columns).
     * A malformed payload or a missing/empty `tables` array yields an empty
     * list rather than throwing. The selection is preserved where possible
     * (clamped into the new list); when there was no valid selection and the
     * new list is non-empty, the first table is selected.
     *
     * @param db_tables_result_json The `db.tables` response `result` JSON (an
     * object with a `tables` array); treated as untrusted and read only through
     * `json_view`.
     */
    void apply_tables(const std::string& db_tables_result_json);

    /**
     * @brief The parsed tables in server order.
     * @return A reference to the table list; valid until the next
     * @ref apply_tables.
     */
    const std::vector<TableInfo>& tables() const;

    /**
     * @brief The index of the currently selected table, if any.
     * @return The selected index, or `std::nullopt` when the list is empty.
     */
    std::optional<std::size_t> selected() const;

    /**
     * @brief Selects the table at @p index.
     *
     * A no-op when the list is empty; otherwise @p index is clamped to the last
     * table so the selection is always valid.
     *
     * @param index Zero-based table index to select.
     */
    void select(std::size_t index);

    /**
     * @brief Builds the `db.rows` request for the selected table and @p query.
     *
     * Emits `db.rows` with `table` (the selected name), `offset` and `limit`
     * always; `q` only when @ref PageQuery::filter is non-empty; and `sort`
     * plus `order` (`asc`/`desc`) only when @ref PageQuery::sort_key is set, so
     * an empty sort never reaches the server. Every string value is escaped
     * with `jsonString`; the bounds mirror the catalog (`limit` 1..500, T-121).
     * When no table is selected the `table` argument is empty (the server
     * resolves it to `not_found`, which the screen surfaces as a toast).
     *
     * @param query The page window, sort and filter to encode.
     * @param request_id The correlation id echoed back in the response.
     * @return A fully-built `db.rows` @ref commands::CommandRequest.
     */
    commands::CommandRequest rows_request(const PageQuery& query,
                                          const std::string& request_id) const;

    /**
     * @brief The right-pane @ref ColumnDef set for the selected table.
     *
     * Maps each selected table column to a @ref ColumnDef whose @ref
     * ColumnDef::key is the column name, with a max width chosen from the
     * column's type/name (an id-like column 12, a url 40, a timestamp 20, an
     * integer 12 and right-aligned, a boolean 8, otherwise a generic string
     * width) and @ref ColumnDef::sortable copied from the column. A column
     * whose name looks like a secret is excluded (see the file-level note). The
     * set is capped at @ref k_max_columns.
     *
     * @return The derived columns in display order; empty when nothing is
     * selected.
     */
    std::vector<ColumnDef> columns_for_selected() const;

    /**
     * @brief The row field keys the right-pane @ref TableModel reads, parallel
     * to @ref columns_for_selected, for @ref rows_from_result.
     * @return One key (column name) per displayed column, in display order.
     */
    std::vector<std::string> column_keys_for_selected() const;

    /**
     * @brief Whether the selected table offers an insert affordance.
     * @return `true` when a table is selected and reports `writable`; `false`
     * otherwise. The insert command mapping lives in the screen (only `links`
     * maps to `link.create` in v1).
     */
    bool can_insert() const;

    /**
     * @brief The number of columns the secret filter hid for the selected
     * table.
     *
     * Lets the screen show the one-line "defence in depth" note when a
     * secret-named column was refused (implementation step 5).
     *
     * @return The count of selected-table columns excluded by the secret
     * filter; `0` when nothing is selected or no column was filtered.
     */
    std::size_t hidden_secret_columns() const;

    /**
     * @brief The left-pane display row for table @p index.
     *
     * Produces the selection marker, the table name left-justified in a fixed
     * name field, and the row count with thousands separators, e.g.
     * `"> links           1,204"`. Server data (the name) is not sanitized
     * here; the screen sanitizes and width-fits the line before display
     * (T-110).
     *
     * @param index Zero-based table index; an out-of-range index yields an
     * empty string.
     * @return The composed left-pane row text.
     */
    std::string left_pane_row(std::size_t index) const;

    /**
     * @brief The fixed D6 explanatory note shown under the left-pane list.
     * @return The note text (no server data), shown always.
     */
    static std::string note_text();

    /**
     * @brief Whether a column name looks like a secret and must not be
     * rendered.
     *
     * Case-insensitive substring match against `hash`, `token`, `salt`,
     * `secret`, `password` and `dsn`, with the single exception of the exact
     * name `client_id_hash` (the only client identifier `click_events`
     * exposes).
     *
     * @param name The column name to classify.
     * @return `true` when @p name is secret-like and should be hidden.
     */
    static bool is_secret_column_name(const std::string& name);

    /**
     * @brief Projects out every secret-named top-level key from one row's JSON,
     * keeping the row-JSON viewer symmetric with @ref columns_for_selected
     * (security review 06.0/14 LOW, defence in depth).
     *
     * Reads @p row_json's top-level members through the sanctioned `json_view`
     * helpers (contract C9) and re-emits a compact object, dropping any member
     * whose key satisfies @ref is_secret_column_name; each kept member's key is
     * re-escaped with `jsonString` and its value copied as the
     * scanner-validated raw token (as `reserialize_object` does), so no
     * untrusted text is spliced.
     *
     * Byte-identity note: the spec's "row JSON is byte-identical to the rows
     * element (modulo `pretty_print` whitespace)" holds whenever @p row_json
     * contains no secret-named field (the `db.rows` columns are fixed,
     * canonically-escaped identifiers, so re-escaping is a no-op); when a
     * secret field is present it is intentionally absent from the output, and
     * the screen shows a one-line note.
     *
     * @param row_json One `db.rows` element's exact JSON object text; treated
     * as untrusted and read only through `json_view`.
     * @param hidden Optional out-param receiving the number of members dropped;
     * `0` when none matched or @p row_json is not a scannable object.
     * @return The compact object with secret-named members removed; @p row_json
     * returned unchanged when it is not a scannable object (including `{}` and
     * malformed input), so a non-object row still displays.
     */
    static std::string redact_secret_fields(const std::string& row_json,
                                            std::size_t* hidden = nullptr);

private:
    ///< The parsed tables in server order (bounded by @ref k_max_tables).
    std::vector<TableInfo> m_tables_;

    ///< The selected table index, or `nullopt` when the list is empty.
    std::optional<std::size_t> m_selected_;
};

}  // namespace url_shortener::tui
