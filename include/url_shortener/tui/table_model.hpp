#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace url_shortener::tui
{

/**
 * @brief Declarative description of one table column (ADR 0003 Decision 2,
 * `TableView`).
 *
 * A pure value type owned by the screen that builds the @ref TableModel. The
 * model never decides what a column *means*; the screen maps a column's
 * @ref key to the command's result field and to its allowed `sort` values.
 */
struct ColumnDef
{
    ///< Result field key this column reads (e.g. `slug`, `stats.total_redirects`
    ///< as a dotted path); also the `sort` value sent when @ref sortable.
    std::string key;

    ///< Human column heading shown in the header row.
    std::string title;

    ///< Minimum width in display columns the layout will allocate when the
    ///< column is shown.
    int min_width = 4;

    ///< Maximum width in display columns; wider content is truncated to fit.
    int max_width = 60;

    ///< `true` when @ref TableModel::toggle_sort may sort by this column; a
    ///< column whose @ref key is not an allowed server `sort` value stays
    ///< `false` so the model never emits an unsupported sort.
    bool sortable = false;

    ///< `true` when the column participates in layout; toggled by
    ///< @ref TableModel::toggle_column (at least one column stays visible).
    bool visible = true;

    ///< `true` to right-justify the cell text (numeric columns such as CLICKS).
    bool right_align = false;
};

/**
 * @brief The query the model asks its handler to run: a page window plus the
 * current sort and filter.
 *
 * Mirrors the catalog's `link.list` / `db.rows` parameters. @ref limit is
 * bounded to 1..500 by the model; @ref offset is a non-negative row offset.
 */
struct PageQuery
{
    ///< Zero-based row offset of the first row on the current page.
    std::size_t offset = 0;

    ///< Page size (rows per page); clamped to 1..500 by the model.
    std::size_t limit = 20;

    ///< Active sort field (a @ref ColumnDef::key), or empty for the server
    ///< default order.
    std::string sort_key;

    ///< `true` for descending order, `false` for ascending.
    bool descending = true;

    ///< Server-side filter text (`q`); empty means no filter.
    std::string filter;
};

/**
 * @brief One page of results handed back to the model via
 * @ref TableModel::apply_result.
 *
 * @ref rows and @ref raw_rows are parallel: `rows[i]` is the per-column cell
 * text for the element whose exact `result` JSON is `raw_rows[i]`.
 */
struct PageResult
{
    ///< Per-row cells, each inner vector aligned to the model's column order
    ///< (including columns that are currently hidden).
    std::vector<std::vector<std::string>> rows;

    ///< The exact `result` JSON of each element, parallel to @ref rows, for the
    ///< "row JSON" view (D6) and detail screens.
    std::vector<std::string> raw_rows;

    ///< Total number of rows across all pages (from the result's `total`), used
    ///< for page-count arithmetic.
    std::size_t total = 0;
};

/**
 * @brief Lifecycle of the model's current page.
 */
enum class LoadState
{
    ///< No query has been emitted yet.
    idle,

    ///< A query is in flight; results have not arrived.
    loading,

    ///< A non-empty page is loaded.
    loaded,

    ///< The query succeeded but returned no rows (`total == 0`).
    empty,

    ///< The last query failed; previous rows are kept (renderer may dim them).
    error
};

/**
 * @brief FTXUI-free state machine behind every paged list in the TUI (Links D3,
 * Tables D6, Jobs D8).
 *
 * The model owns page/offset arithmetic, sort toggling, filter text, column
 * visibility, the horizontal column window, deterministic width allocation and
 * stale-response protection. It never performs I/O: it emits a @ref PageQuery
 * through the handler set with @ref set_query_handler and expects the screen to
 * run it off the UI thread and feed the outcome back through @ref apply_result
 * / @ref apply_error. Each emitted query carries a strictly increasing
 * `generation`; a result for any generation other than the latest is ignored so
 * quick filter typing cannot show a stale page (threat model T-121).
 *
 * The model is pure (no FTXUI, no threads, no clocks): it is driven and
 * observed entirely through its methods and is snapshot/unit-testable without a
 * terminal (ADR 0003 rule 6).
 */
class TableModel
{
public:
    /**
     * @brief Signature of the sink the model calls whenever it needs data.
     *
     * The screen runs @p query off the UI thread and, when the response
     * arrives, calls @ref apply_result / @ref apply_error with the same
     * @p generation. It must not run the query synchronously inside this call.
     */
    using QueryHandler =
        std::function<void(const PageQuery& query, std::uint64_t generation)>;

    /**
     * @brief Builds a model over @p columns with the given default page size.
     *
     * No query is emitted during construction (the handler is not set yet); the
     * screen calls @ref set_query_handler then @ref refresh to perform the
     * initial load.
     *
     * @param columns The column set, in display order; copied and owned.
     * @param page_size Rows per page; clamped to 1..500. Defaults to 20 (the D3
     * "20 per page").
     */
    explicit TableModel(std::vector<ColumnDef> columns,
                        std::size_t page_size = 20);

    /**
     * @brief Sets the handler invoked whenever the model needs data.
     *
     * Setting the handler does not itself emit a query; call @ref refresh to
     * perform the first load.
     *
     * @param handler The query sink; moved in. May be empty, in which case the
     * model updates its query state but emits nothing.
     */
    void set_query_handler(QueryHandler handler);

    /**
     * @brief Re-emits the current query with a fresh generation.
     *
     * Moves the model into @ref LoadState::loading and calls the handler with
     * the unchanged @ref query. Used for the `r` refresh key and to recover
     * after an error.
     */
    void refresh();

    /**
     * @brief Applies a page of results if it answers the latest query.
     *
     * @param generation The generation the result answers.
     * @param result The page to apply; moved in when accepted.
     * @return `true` when @p generation is the latest emitted one and the page
     * was applied; `false` (ignored) when @p generation is stale, so an
     * out-of-order or superseded response cannot overwrite newer state. On
     * success the state becomes @ref LoadState::empty when the page is empty
     * with `total == 0`, otherwise @ref LoadState::loaded, and the selection is
     * clamped into the new page.
     */
    bool apply_result(std::uint64_t generation, PageResult result);

    /**
     * @brief Records a query failure if it answers the latest query.
     *
     * Moves the model into @ref LoadState::error with @p message and keeps the
     * previously loaded rows and selection so the renderer can dim rather than
     * blank the table. A stale @p generation is ignored.
     *
     * @param generation The generation the error answers.
     * @param message The error text to display (already sanitized by the caller
     * where it originates from server data, T-110).
     */
    void apply_error(std::uint64_t generation, std::string message);

    /// @name Row selection (within the current page; never emits a query)
    /// @{
    /// Moves the selection to the next row, clamped to the last row.
    void select_next();
    /// Moves the selection to the previous row, clamped to the first row.
    void select_prev();
    /// Selects the first row of the current page.
    void select_first();
    /// Selects the last row of the current page.
    void select_last();
    /// @}

    /// @name Paging (emits a query only when the page actually changes)
    /// @{
    /// Loads the next page; a no-op (no query) when already on the last page.
    void next_page();
    /// Loads the previous page; a no-op (no query) when already on page 1.
    void prev_page();
    /// Loads the first page; a no-op (no query) when already on page 1.
    void first_page();
    /// Loads the last page; a no-op (no query) when already on the last page.
    void last_page();
    /// @}

    /**
     * @brief Cycles the sort for a column and re-queries from page 1.
     *
     * A non-sortable column (or an out-of-range index) is ignored and emits no
     * query. The first sort of a column is ascending; sorting the same column
     * again flips between descending and ascending (none -> asc -> desc ->
     * asc...). Switching to a different column starts ascending. Changing the
     * sort resets the offset to 0.
     *
     * @param column_index Index into the model's column list.
     */
    void toggle_sort(std::size_t column_index);

    /**
     * @brief Sets the server-side filter, resets to page 1 and re-queries.
     *
     * @param text The new filter text (`q`); moved in. Always emits a query so
     * the page reflects the current filter.
     */
    void set_filter(std::string text);

    /**
     * @brief Toggles a column's visibility (no query; layout only).
     *
     * Hiding is refused when the column is the last visible one, so at least one
     * column always remains. An out-of-range index is ignored. The horizontal
     * window is re-clamped to the new visible set.
     *
     * @param column_index Index into the model's column list.
     */
    void toggle_column(std::size_t column_index);

    /**
     * @brief Moves the horizontal column window over the visible columns.
     *
     * The window's first-visible-column index is moved by @p delta and clamped
     * so at least one visible column is always shown; it never scrolls past
     * either end. Emits no query.
     *
     * @param delta Signed number of visible columns to move the window by.
     */
    void scroll_columns(int delta);

    /**
     * @brief Computes the display widths of the columns shown in the current
     * horizontal window for a given terminal width.
     *
     * Each shown column is allocated `clamp(max content/header width over the
     * current rows, min_width, max_width)`, trimmed so the running total plus
     * one-space separators never exceeds @p available_width. At least one column
     * is always returned (truncated to the width if it alone is too wide). The
     * allocation is deterministic.
     *
     * @param available_width The terminal width, in columns, available to the
     * table body.
     * @return One width per shown column, in display order; empty only when
     * @p available_width is non-positive or there are no columns.
     */
    std::vector<int> layout(int available_width) const;

    /**
     * @brief The column definitions shown for @p available_width, parallel to
     * @ref layout.
     *
     * @param available_width The terminal width available to the table body.
     * @return Non-owning pointers into the model's columns for the shown window,
     * in display order. Valid until the column set changes.
     */
    std::vector<const ColumnDef*> shown_columns(int available_width) const;

    /**
     * @brief Whether visible columns exist to the left of the shown window.
     *
     * @param available_width The terminal width available to the table body.
     * @return `true` when a `<` overflow marker should be drawn.
     */
    bool overflow_left(int available_width) const;

    /**
     * @brief Whether visible columns exist to the right of the shown window.
     *
     * @param available_width The terminal width available to the table body.
     * @return `true` when a `>` overflow marker should be drawn.
     */
    bool overflow_right(int available_width) const;

    /// @return Zero-based index of the current page.
    std::size_t page_index() const;

    /// @return Total number of pages; at least 1 even when empty.
    std::size_t page_count() const;

    /// @return Total row count across all pages (the last result's `total`).
    std::size_t total() const;

    /// @return The selected row index within the current page, or `nullopt`
    /// when the page is empty.
    std::optional<std::size_t> selected() const;

    /// @return The raw `result` JSON of the selected row, or `nullptr` when
    /// nothing is selected. Valid until the next @ref apply_result.
    const std::string* selected_raw() const;

    /// @return The current load state.
    LoadState state() const;

    /// @return The last error message; empty unless @ref state is
    /// @ref LoadState::error.
    const std::string& error_message() const;

    /// @return The current query (offset, limit, sort, filter).
    const PageQuery& query() const;

    /// @return The current page's per-column cells.
    const std::vector<std::vector<std::string>>& rows() const;

    /// @return The model's column definitions in display order, including
    /// currently hidden ones. Valid until the model is destroyed; the column
    /// chooser (06.0/10) reads titles and visibility through it. The backing
    /// storage is contiguous, so a pointer returned by @ref shown_columns can be
    /// turned into an index via `ptr - columns().data()`.
    const std::vector<ColumnDef>& columns() const;

private:
    /**
     * @brief Bumps the generation, enters @ref LoadState::loading and calls the
     * query handler (when set) with the current query.
     */
    void emit_query();

    /**
     * @brief Indices into @ref m_columns_ of the currently visible columns, in
     * order.
     */
    std::vector<std::size_t> visible_indices() const;

    /**
     * @brief Clamps @ref m_window_start_ into the visible-column range.
     */
    void clamp_window();

    /**
     * @brief Desired width for a column: max of header and current-row content
     * widths, clamped to the column's [min_width, max_width].
     *
     * @param column_index Index into @ref m_columns_.
     * @return The clamped desired display width.
     */
    int desired_width(std::size_t column_index) const;

    /**
     * @brief Shared window computation for @ref layout / @ref shown_columns.
     *
     * @param available_width The terminal width available to the table body.
     * @return Pairs of (column index into @ref m_columns_, allocated width) for
     * the shown window, in display order.
     */
    std::vector<std::pair<std::size_t, int>>
    compute_window(int available_width) const;

    ///< Column definitions in display order.
    std::vector<ColumnDef> m_columns_;

    ///< The current query state (offset, limit, sort, filter).
    PageQuery m_query_;

    ///< The current page of results.
    PageResult m_result_;

    ///< Current load lifecycle state.
    LoadState m_state_ = LoadState::idle;

    ///< Last error message; empty unless @ref m_state_ is error.
    std::string m_error_;

    ///< Sink called when the model needs data; may be empty.
    QueryHandler m_handler_;

    ///< Generation of the most recently emitted query; results must match it.
    std::uint64_t m_generation_ = 0;

    ///< Selected row within the current page, or `nullopt` when empty.
    std::optional<std::size_t> m_selected_;

    ///< First visible-column position shown by the horizontal window.
    std::size_t m_window_start_ = 0;
};

}  // namespace url_shortener::tui
