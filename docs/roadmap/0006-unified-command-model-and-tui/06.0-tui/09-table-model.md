# 09 - TableModel: paging, sorting, filtering, columns, horizontal scroll

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/02, 06.0/03
**Blocks:** 10, 12, 14, 16, 20
**Estimated size:** M
**Suggested agent:** cpp-expert, testing-expert

## Objective

Implement the FTXUI-free state machine behind every paged list in the TUI
(ADR 0003 Decision 2: "TableView: paged rows -> FTXUI table, sort/filter/search")
and ADR 0003's stated negative consequence ("very wide tables need horizontal
scrolling and column hiding that we implement in `TableView`"). The model owns
page/offset arithmetic, sort toggling, filter text, column visibility, the
horizontal column window, width allocation and truncation, and stale-response
protection. It is used by **D3** (Links), **D6** (Tables browser) and **D8** (Jobs).

## Dependencies

- Internal: 06.0/02 (`json_view::split_array`, `get_string`, `get_int`, over the 01.0/01 scanner), 06.0/03 (generation/cancel pattern; the model itself only emits query intents).
- External: none (model lib).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/tui/table_model.hpp` / `src/tui/table_model.cpp` | create | `TableModel`, `ColumnDef`, `PageQuery`, `PageResult`, `LoadState` |
| `include/url_shortener/tui/text_fit.hpp` / `src/tui/text_fit.cpp` | create | `display_width`, `truncate_to_width` (UTF-8 aware, ellipsis `...`), `pad_to_width`, `sanitize_for_terminal` (T-110 central terminal-control sanitizer; no FTXUI) |
| `tests/unit/tui/support/hostile_strings.hpp` | create | shared hostile-data fixture for T-110 (`ESC[2J`, `OSC 52` clipboard write, `U+202E` bidi override, `U+200B` zero-width, a C1 byte, invalid UTF-8, a 64 KiB field); reused by every render-sink test (10, 08, 06, 11, 13, 14, 16) |
| `include/url_shortener/tui/row_mapper.hpp` / `src/tui/row_mapper.cpp` | create | `rows_from_result(result_json, column_keys, items_field)` using `json_view`; nested key paths `stats.total_redirects` |
| `tests/unit/tui/09_table_model_test.cpp`, `09_text_fit_test.cpp`, `09_row_mapper_test.cpp` | create | see Tests |
| `sources.cmake` | modify | register |

## Classes and functions

```cpp
struct ColumnDef { std::string key; std::string title; int min_width = 4; int max_width = 60;
                   bool sortable = false; bool visible = true; bool right_align = false; };
struct PageQuery { std::size_t offset = 0; std::size_t limit = 20; std::string sort_key; bool descending = true; std::string filter; };
struct PageResult { std::vector<std::vector<std::string>> rows; std::vector<std::string> raw_rows; std::size_t total = 0; };
enum class LoadState { idle, loading, loaded, empty, error };

class TableModel {                                       // NEW
public:
    TableModel(std::vector<ColumnDef> columns, std::size_t page_size = 20);
    using QueryHandler = std::function<void(const PageQuery&, std::uint64_t generation)>;
    void set_query_handler(QueryHandler h);              // called whenever the model needs data
    void refresh();                                      // re-emit current query
    bool apply_result(std::uint64_t generation, PageResult r);   // false (ignored) if stale
    void apply_error(std::uint64_t generation, std::string message);

    void select_next();  void select_prev();  void select_first();  void select_last();   // within page
    void next_page();  void prev_page();  void first_page();  void last_page();
    void toggle_sort(std::size_t column_index);           // none -> asc -> desc -> asc ...; non-sortable ignored
    void set_filter(std::string text);                   // resets offset to 0, emits query
    void toggle_column(std::size_t column_index);        // at least one column stays visible
    void scroll_columns(int delta);                      // horizontal window over visible columns
    std::vector<int> layout(int available_width) const;  // widths for the currently shown column window
    // views for the renderer
    std::vector<const ColumnDef*> shown_columns(int available_width) const;
    std::size_t page_index() const;  std::size_t page_count() const;  std::size_t total() const;
    std::optional<std::size_t> selected() const;  const std::string* selected_raw() const;
    LoadState state() const;  const std::string& error_message() const;
    const PageQuery& query() const;  const std::vector<std::vector<std::string>>& rows() const;
};
std::size_t display_width(const std::string& utf8);                   // wide CJK = 2, combining = 0 (best effort; FTXUI cell widths VERIFY AT PIN TIME)
std::string truncate_to_width(const std::string& utf8, std::size_t width);   // appends "..." when cut, never splits a code point
std::string sanitize_for_terminal(std::string_view in, std::size_t max_bytes = 64 * 1024);  // T-110: neutralize terminal-control bytes BEFORE width fitting (see step 0); pure, FTXUI-free, bounded output
```

`generation` increments with every emitted query; `apply_result` ignores
results with older generations so quick filter typing cannot show stale pages.

## Implementation steps

0. `sanitize_for_terminal` (T-110, CRITICAL - the task-wide terminal-control sanitizer every render sink must call on server/stored strings *before* width fitting): decode input as UTF-8 (invalid bytes -> `U+FFFD`); drop/neutralize `ESC` (0x1B) and every CSI/OSC/DCS/APC/PM/SOS introducer **and its whole payload** (through the terminator, e.g. `BEL`/`ST`); drop DEL (0x7F); map the remaining C0 controls to a single space (tabs and newlines included - no control is "needed" in a cell/label); drop the C1 controls whether they arrive as raw 0x80-0x9F or as code points `U+0080`-`U+009F`; strip bidi controls `U+202A`-`U+202E`, `U+2066`-`U+2069`, `U+200E`/`U+200F`, `U+061C`; strip zero-width/invisible `U+200B`-`U+200D`, `U+2060`, `U+FEFF`; and cap the result at `max_bytes` (no code point split at the cut). Output is therefore printable text safe to hand to FTXUI `text()`. It runs ahead of `display_width`/`truncate_to_width`, so a truncated cell can never re-expose a half-stripped escape. Confirm-token *comparison* (typed-slug delete, 06) stays on the raw bytes; only the *displayed* token is sanitized.
1. Page arithmetic: `page_count = max(1, ceil(total / limit))`; `last_page` offset = `(page_count-1)*limit`; selection clamps to `rows.size()-1`; after a page change selection resets to 0 (or last row for `prev_page` via `select_last`, choose first row for simplicity).
2. `toggle_sort` writes `PageQuery.sort_key` from `ColumnDef.key`; the screen maps keys to the command's allowed `sort` values (e.g. `link.list` allows only `created_at|slug|updated_at`, so non-allowed columns are created with `sortable=false`).
3. `layout`: distribute width: each visible column gets `clamp(max content width among shown rows, min_width, max_width)`; if sum + separators exceeds `available_width`, shrink the widest columns down to `min_width`, then drop columns from the right *into the horizontal window* (`scroll_columns` moves the first shown column index); always show at least one column and mark `<`/`>` overflow in `overflow_left()/overflow_right()`.
4. `truncate_to_width` appends `...` (3 chars) as in D3 (`...utm_source=tw...`); mono-safe.
5. `rows_from_result` reads `items` (for `link.list`) or `rows` (for `db.rows`) via `raw_field` + `split_array`, then per-column `get_string`/`get_int`, dotted keys by nested `raw_field`; missing -> `-` (D3 uses `-` for empty expires). Keep each element's raw JSON in `PageResult.raw_rows` for the "row JSON" view (D6) and detail screens.
6. `apply_result` with `rows.empty()` and `total == 0` -> `LoadState::empty`; error -> `error` (keeps previous rows visible but dimmed - renderer decision).

## Commands

```powershell
cmake --build cmake-build --target tui__09_table_model_test tui__09_text_fit_test tui__09_row_mapper_test
ctest --test-dir cmake-build -R "^tui__09_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `09_table_model_test.cpp` | `tui__09_table_model_test` | unit | page boundaries: total 0 / 1 / 20 / 21 / 1204 (-> 61 pages as in D3); `last_page` offset; `next_page` at last is a no-op (no query emitted); sort cycle asc/desc and non-sortable ignored; `set_filter` resets offset; stale `apply_result` ignored; error then refresh recovers; hiding all columns refused; `scroll_columns` clamps both ends; selection clamp when a shorter page arrives; query count assertions |
| `09_text_fit_test.cpp` | `tui__09_text_fit_test` | unit | ASCII, multibyte (UTF-8 accents, CJK width 2), emoji, width 0/1/2/3 (no negative sizes), exact fit not truncated, invalid UTF-8 bytes handled, combining marks; **`sanitize_for_terminal` (T-110)** over `support/hostile_strings.hpp`: output contains no `ESC`/C0-except-space/DEL/C1 byte, no CSI/OSC/DCS payload survives (`ESC[2J`, `OSC 52` clipboard write both gone), bidi (`U+202E`) and zero-width (`U+200B`)/BOM stripped, invalid UTF-8 -> `U+FFFD`, 64 KiB input bounded by `max_bytes`, benign text (incl. CJK/emoji) passes through unchanged, and sanitize-then-`truncate_to_width` never re-exposes a control byte |
| `09_row_mapper_test.cpp` | `tui__09_row_mapper_test` | unit | real-shaped `link.list` result with 3 items (nested `stats.total_redirects`); missing field -> `-`; strings containing `]`/`}`/escaped quotes; `db.rows` shape; empty `items`; malformed JSON -> error state, not crash |

## Constraints

- Pure: no FTXUI, no threads, no clocks (C8; ADR 0003 rule 6 testability).
- `sanitize_for_terminal` is this task's single terminal-control choke point (T-110): it lives here (no FTXUI dependency), is bounded (`O(bytes)`, output <= `max_bytes`), and every render subtask that prints server/stored strings (10, 08, 06, 11, 13, 14, 16) must route them through it. The model layer never emits OSC-52 or any escape of its own.
- Limits mirror the catalog: `limit` 1..500, default page 20 in the UI (D3 "20 per page"); `offset >= 0`.
- The model never decides *what* a column means: all command-specific mapping lives in screens (12, 14, 16).

## Success criteria

1. 61-page D3 scenario (total 1204, limit 20) reproduced exactly: `page 1/61`.
2. No query is emitted for no-op navigation.
3. Stale responses cannot overwrite newer state (generation test with out-of-order results).
4. Width layout is deterministic and never exceeds `available_width`, for widths 20..200 (property loop).
5. `sanitize_for_terminal` reduces every entry of the shared hostile-data fixture to printable text with no raw `ESC`/C1/bidi/zero-width bytes and bounded length (T-110), and this is the helper the render sinks apply.

## Out of scope / risks

- Cursor-based pagination, multi-column sort, client-side filtering (filtering is server-side via `q`).
- Risk: `display_width` heuristics differ from FTXUI's cell width (`ftxui::string_width`, VERIFY AT PIN TIME); if FTXUI exposes it, prefer it in the UI lib and keep this function for the model's tests only.
