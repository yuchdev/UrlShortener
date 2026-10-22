# 10 - TableView renderer and interaction (D3/D6 table body)

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/04, 05, 09
**Blocks:** 12, 14, 16, 20
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Render a `TableModel` with FTXUI as the reusable table body of **D3** (Links),
**D6** (right pane) and **D8** (Jobs): header row, selection marker `>`,
truncated cells, footer `page N/M (20 per page)`, loading/empty/error states,
horizontal-overflow indicators, filter input, and the keys `j/k`, arrows,
PgUp/PgDn, `g/G`, `/` filter, `s` sort, `c` columns dialog, `<`/`>` horizontal
scroll, `r` refresh. Screens add their own action keys through a callback.

## Dependencies

- Internal: 06.0/09 (`TableModel`, `text_fit`), 05 (`Theme`), 04 (`ScreenContext.text_input_active`, `KeyHint`).
- External FTXUI: `ftxui::Table` from `ftxui/dom/table.hpp` (`Table(std::vector<std::vector<std::string>>)`, `.SelectAll()`, `.SelectRow(i)`, `.SelectColumn(i)`, `.Border(BorderStyle)`, `.Separator...`, `.Decorate(Decorator)`, `.Render()`; method names **VERIFY AT PIN TIME**), `ftxui::Component`, `Renderer`, `CatchEvent`, `Input` (filter), `Checkbox` (column chooser), `Event::PageUp/PageDown/ArrowUp/ArrowDown/Home/End`, `text`, `inverted`, `dim`, `hbox`, `vbox`, `flex`, `size`, `xflex`. If `Table` proves awkward for per-cell truncation, build rows manually with `hbox` + `size(WIDTH, EQUAL, w)` per cell (same golden output).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/tui/table_view.hpp` | create | `TableViewOptions`, `make_table_view(TableModel&, TableViewOptions)` |
| `src/tui/table_view.cpp` | create | rendering + events |
| `src/tui/column_chooser.cpp` | create | `c` dialog: `Container::Vertical` of `Checkbox`, one per column |
| `tests/unit/tui/10_table_view_render_test.cpp`, `10_table_view_keys_test.cpp` | create | see Tests |
| `tests/unit/tui/golden/d03_table_20x_80x24.txt`, `table_loading_80x12.txt`, `table_empty_80x12.txt`, `table_error_80x12.txt`, `table_hscroll_left_80x12.txt`, `table_hscroll_right_80x12.txt`, `table_mono_80x12.txt`, `table_column_chooser_80x24.txt` | create | goldens |
| `sources.cmake` | modify | register |

## Classes and functions

```cpp
struct TableViewOptions {                                // NEW
    std::string title;                                   // "Links", "links  (1,204 rows)"
    const Theme* theme = nullptr;
    bool show_filter_bar = true;
    std::function<void(std::size_t row_index)> on_activate;        // Enter
    std::function<bool(const ftxui::Event&, std::optional<std::size_t> row)> on_extra_key;   // n/e/d/t/u/p/s ...
    std::vector<KeyHint> extra_hints;                    // merged into the footer
    std::function<ftxui::Element()> header_extra;        // e.g. D3 status radio on the title row
};
ftxui::Component make_table_view(TableModel& model, TableViewOptions options);
std::string page_footer(const TableModel& model);        // "page 1/61  (20 per page)" (model-side string, unit-tested without FTXUI)
```

Event routing order: column chooser (if open) -> filter `Input` (if focused, Esc/Enter return focus) -> `on_extra_key` -> built-in navigation. `on_extra_key` is consulted before built-ins only for keys the screen declares, so `s` can mean "sort" in D6 and "stats" in D3 (D3 footer: `s stats`; D6 footer: `s sort`); the options struct therefore has `bool sort_key_enabled = true` which Links sets false.

## Implementation steps

1. Build `std::vector<std::vector<std::string>>` from `model.rows()` using `model.layout(width)` and `truncate_to_width`; header row bold/`header_row` style; selected row `inverted` with `>` prefix column.
2. Obtain the available width from `Terminal::Size()` at render time through an `ftxui::Element` using `Elements` that read `Box` is unreliable; instead wrap in a custom `Node`/`Renderer` that receives the final width: use `ftxui::size(...)` with `flex` and a measuring `Renderer` that stores `Box` via `reflect(box)` (`ftxui::reflect`, VERIFY AT PIN TIME) then lays out on the next frame; the golden test renders twice (first frame measures).
3. State rows: `loading` -> dim "loading..."; `empty` -> "no rows"; `error` -> `! message` in error style plus hint `r retry`.
4. Footer line: left `page_footer`, right overflow marker `< more >` when `overflow_left/right`.
5. Wire keys to the model (`select_next` etc.); `/` focuses a single-line `Input` bound to a string and calls `set_filter` on Enter (not on each keystroke) to avoid flooding the backend; Esc clears.
6. Column chooser: `Checkbox` per `ColumnDef` calling `toggle_column`; Enter/Esc closes.

## Commands

```powershell
cmake --build cmake-build --target tui__10_table_view_render_test tui__10_table_view_keys_test
ctest --test-dir cmake-build -R "^tui__10_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `10_table_view_render_test.cpp` | `tui__10_table_view_render_test` | unit | D3-shaped body (5 columns, 4 rows incl. a long URL truncated with `...`, `page 1/61  (20 per page)`) matches golden; loading/empty/error states; horizontal scroll left/right goldens (ensures `<`/`>` markers); 80x12 and 120x30 widths; mono theme ASCII-only; zero columns visible impossible; cell with newline/tab sanitised to a space |
| `10_table_view_keys_test.cpp` | `tui__10_table_view_keys_test` | unit | `j`,`k`,Down,Up,PgDn,PgUp,`g`,`G` map to model calls and emit queries as expected (count queries via the handler); `/` + text + Enter sets filter and emits exactly one query; Esc clears filter; `s` toggles sort on the sorted column; `c` opens chooser, toggling hides the column in the next render; extra-key callback receives selected row; keys ignored while `text_input_active`; `r` refresh emits one query |

## Constraints

- No command knowledge: columns and mapping come from the owning screen (C1, C8).
- Rendering is a function of `TableModel` state only - no hidden state, so goldens are reproducible.
- Must work at 80x24 (D3 width) and degrade (hscroll) below; mono theme supported.
- No blocking calls; data arrives via `TableModel::apply_result` posted by the screen.

## Success criteria

1. The golden for the D3 body is byte-identical to the corresponding rows of D3 in ADR 0003 (header, `>` marker, `-` for empty cells, footer).
2. Navigating all keys never throws on empty and single-row tables.
3. Horizontal scroll reveals every column of a 12-column fixture at width 80.
4. Filter typing produces one query per Enter, not per keystroke.

## Out of scope / risks

- Mouse selection, cell editing, column resize by user, export of the table view.
- Risk: `ftxui::Table` API drift - hence the manual `hbox` fallback.
- Risk: measuring available width needs a frame of latency; mitigated by rendering twice in tests and by using `Terminal::Size()` as the first-frame guess.
