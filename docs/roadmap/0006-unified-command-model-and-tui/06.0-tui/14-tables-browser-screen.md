# 14 - Database tables browser screen (D6)

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/03, 04, 05, 08, 09, 10, 11; 03.0/15 (`db.tables`, `db.rows`), 03.0/16 (`links`, `click_events` sources)
**Blocks:** 06.0/18, 20
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Render **D6**: left pane list of logical tables with row counts (`links 1,204`,
`click_events 912,550`, `jobs 3`) and the explanatory note "read-only logical
views over storage ports; no auth/secret tables"; right pane a paged,
sortable, searchable `TableView` over `db.rows`, with `/ search`, `s sort`,
`c columns`, `Enter row JSON`, and `n insert row -> New link form` only when
the selected table reports `writable: true` (`links`). Implements the D11
edges `Tables -> RowJson` and `Tables -> LinkForm`.

## Dependencies

- Internal: 06.0/09-10 (`TableModel`/`TableView`), 08 (`FormScreen` for insert), 11 (`ResultViewer` for row JSON), 03 (`AsyncCaller`), 05 (theme).
- Commands: `db.tables` (result `{tables:[{name,rows,writable,columns:[{name,type}]}]}`) and `db.rows` (`{table,columns,rows,offset,limit,total}`, args `table`, `q`, `sort`, `order`, `offset`, `limit`) from catalog 3.2. Allowed sort columns are those the server lists; the TUI offers sorting on a column only if `db.tables` marks it sortable. Task 03.0 subtask 15's `TableColumn` carries `sortable`/`searchable`, but the catalog's column object lists `name`/`type` only; v1 treats every returned column as sortable and relies on the server's `invalid_field` -> toast, and the additive `sortable` key is proposed for catalog 3.2 (Task 08.0 subtask 03). The `jobs` table (Task 05.0 subtask 21) is read-only like `click_events`.
- External FTXUI: `Menu(std::vector<std::string>*, int*)`, `Container::Horizontal`, `window`, `hbox`, `vbox`, `separator`, `flex`, plus subtask 10 component.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/tui/tables_model.hpp` / `src/tui/tables_model.cpp` | create | `TablesModel`: parses `db.tables`, builds `db.rows` requests, derives `ColumnDef`s from result `columns` (model lib) |
| `src/tui/screens/tables_screen.cpp` | create | `TablesScreen : IScreen` (UI lib) |
| `src/tui/screens/row_json_screen.cpp` | create | `RowJsonScreen` using `ResultViewerModel` over the exact stored `raw_rows[i]` |
| `tests/unit/tui/14_tables_model_test.cpp`, `14_tables_screen_test.cpp` | create | see Tests |
| `tests/unit/tui/golden/d06_tables_80x24.txt`, `d06_tables_events_80x24.txt`, `d06_tables_row_json_80x24.txt`, `d06_tables_empty_80x24.txt`, `d06_tables_error_80x24.txt` | create | goldens |
| `sources.cmake` | modify | register |

## Classes and functions

```cpp
struct TableInfo { std::string name; std::uint64_t rows = 0; bool writable = false; std::vector<std::pair<std::string,std::string>> columns; };
class TablesModel {                                        // NEW
public:
    void apply_tables(const std::string& db_tables_result_json);
    const std::vector<TableInfo>& tables() const;
    std::optional<std::size_t> selected() const;  void select(std::size_t i);
    commands::CommandRequest rows_request(const PageQuery&, const std::string& request_id) const;
    //  args: table, q (filter), sort/order only when sort_key set, offset, limit (20)
    std::vector<ColumnDef> columns_for_selected() const;     // from db.tables column list, max_width by type (id 12, url 40, ts 20)
    bool can_insert() const;                                 // writable && table == "links" is NOT hard-coded: insert is offered when writable; the form command is "link.create"
    std::string left_pane_row(std::size_t i) const;          // "> links           1,204"
    static std::string note_text();                          // D6 note, shown always
};
```

Insert mapping lives in one place: writable table -> command `link.create`
(only `links` is writable in v1). The mapping is a small table in the screen
(`{"links","link.create"}`) because `db.tables` does not say which command inserts; documented as a known coupling and flagged as ADR 0003 gap.

## Implementation steps

1. `on_enter`: `db.tables` -> populate left pane; selecting a table (arrows/Enter in left pane, Tab to switch pane) triggers `db.rows` page 1 and resets sort/filter.
2. Right pane title `links  (1,204 rows)  sort: created_at v` (sort arrow `v` desc / `^` asc); counts formatted with thousands separators.
3. `/` search sets `q`; `s` cycles sort across visible columns; `c` column chooser (10); `Enter` pushes `row_json` with `pretty_print(raw_rows[i])` - "exact `result` item the command returned" (D6 caption).
4. `n`: only when `can_insert()`; opens `link.create` form; on success refresh the table and `db.tables` counts.
5. Secret safety is server-side (C6), but the screen additionally refuses to render a column whose name matches `hash|token|salt|secret|password|dsn` (case-insensitive) except `client_id_hash`, with a one-line note - defence in depth; test with a hostile mock.
6. Unknown table error `not_found` from `db.rows` -> toast; left pane reloads.

## Commands

```powershell
cmake --build cmake-build --target tui__14_tables_model_test tui__14_tables_screen_test
ctest --test-dir cmake-build -R "^tui__14_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `14_tables_model_test.cpp` | `tui__14_tables_model_test` | unit | parse real-shaped `db.tables` (3 tables); `rows_request` bytes for default, with `q`, with sort desc; empty tables list; `can_insert` true only for writable; thousands formatting; columns derived with sensible widths; hostile column names filtered (`password_hash`, `api_token`, `salt`) while `client_id_hash` allowed |
| `14_tables_screen_test.cpp` | `tui__14_tables_screen_test` | unit | D6 golden (3 tables, links selected, 3 rows); switching to `click_events` hides the `n insert` hint (golden) and `n` sends nothing; `Enter` shows the exact raw row JSON; `/` search sends `q`; sort toggles; page keys call `db.rows` with the right `offset`; server `not_found` handled; read-only session still browses (reads are allowed) but insert is disabled with reason; mono golden |

## Constraints

- C6: `db.*` allowlist is enforced by the server; the TUI never constructs table names itself except from `db.tables` output.
- D6: `click_events` and `jobs` are strictly read-only in the UI (no insert/edit/delete affordance).
- C8: JSON via `json_view` (over the 01.0/01 scanner); no repository types.
- Pagination bounds mirror the catalog (`limit` 1..500).

## Success criteria

1. `d06_tables_80x24.txt` reproduces D6's two-pane layout including the left-pane note.
2. Row JSON text is byte-identical to the element in the mock `rows` array (modulo `pretty_print` whitespace, asserted by minifying both).
3. No request other than `db.tables`, `db.rows`, `link.create` is ever sent from this screen.
4. A column named like a secret never appears on screen.

## Out of scope / risks

- Editing/deleting rows from this view, SQL console, schema browsing beyond `columns`, export (use Import/Export screen).
- Risk: counts for `click_events` (912,550) may be expensive for some backends; the screen shows `...` until `db.tables` returns and never polls it automatically.
- Risk: `db.tables` lacks a `sortable` hint and an insert-command hint (see Dependencies).
