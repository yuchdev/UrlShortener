# 11 - Command Palette (D9) and generic command screen

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/02, 03, 04, 05, 06, 07, 08
**Blocks:** 06.0/12, 13, 14, 15, 16 (result viewer), 18, 20
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Make **every** registry command reachable without bespoke code (ADR 0003
rule 2, D9 caption "every command, always reachable"). Implements the
Command Palette overlay (`:` key; filter box `: link.[ ]`, list with name,
summary, `read`/`write` tag, `(confirm)` and `(jobs)` markers) and the
`GenericCommandScreen` of **D11** (Palette -> GenericForm -> Confirm -> result):
form from subtask 08, then a result viewer that pretty-prints the raw
`result` / `job` / `error` of the response.

## Dependencies

- Internal: 06.0/02 (`ICommandClient::commands()`), 07-08 (`FormModel`, `FormScreen`, `run_mutation`), 06 (toasts, confirm), 05 (theme), 04 (`Router`: `palette -> generic_form -> confirm`).
- External FTXUI: `Input` (query), `Menu(std::vector<std::string>*, int*, MenuOption)` or a custom `Container::Vertical` of `Renderer` rows for the multi-column rows, `Modal`-style `dbox` + `clear_under`, `yframe`/`vscroll_indicator` for the result viewer, `Checkbox` ("edit as raw JSON").

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/tui/palette_model.hpp` / `src/tui/palette_model.cpp` | create | `PaletteModel` (model lib) |
| `include/url_shortener/tui/result_viewer_model.hpp` / `src/tui/result_viewer_model.cpp` | create | line-wrapped, scrollable pretty JSON (model lib) |
| `src/tui/screens/palette_screen.cpp` | create | `PaletteScreen : IScreen` (UI lib) |
| `src/tui/screens/generic_command_screen.cpp` | create | `GenericCommandScreen : IScreen`; includes raw-JSON args mode |
| `tests/unit/tui/11_palette_model_test.cpp`, `11_result_viewer_model_test.cpp`, `11_palette_render_test.cpp`, `11_generic_command_flow_test.cpp` | create | see Tests |
| `tests/unit/tui/golden/d09_palette_empty_80x24.txt`, `d09_palette_link_prefix_80x24.txt`, `d09_palette_readonly_80x24.txt`, `d09_result_view_80x24.txt` | create | goldens |
| `sources.cmake` | modify | register |

## Classes and functions

```cpp
struct PaletteEntry { const CommandInfo* info; std::string tag;       // "read" | "write" | "write (confirm)" | "read (jobs)"...
                      bool enabled; std::string disabled_reason; int score; };
class PaletteModel {                                                  // NEW
public:
    PaletteModel(std::vector<CommandInfo> commands, bool read_only);
    void set_query(std::string q);                                    // leading ':' stripped
    const std::vector<PaletteEntry>& entries() const;                 // ranked, stable
    void select_next(); void select_prev();
    const PaletteEntry* selected() const;
    static int score(const std::string& query, const CommandInfo& c); // exact > prefix > dotted-segment prefix > subsequence > summary substring; 0 = hidden
};
class ResultViewerModel {                                             // NEW
public:
    explicit ResultViewerModel(std::string json, std::size_t width);
    void scroll(int delta); void page(int delta);
    std::vector<std::string> visible_lines(std::size_t height) const;
};
```

D9 grouping is reproduced by sorting ties by command name; grouping rows such as
`link.enable / link.disable / ...` in one line is a *display* compaction applied
only when the query is empty (golden `d09_palette_empty`), driven by the
command-name prefix, not by a hard-coded list.

## Implementation steps

1. `PaletteModel` built from `ICommandClient::commands()` (so it includes commands added later, e.g. `auth.*`, with no change); sort: score desc then name.
2. Disabled entries (write/migrate commands in read-only mode) are shown dim with reason `not_authorized (read-only session)` and cannot be activated (18 reuses this).
3. Enter on an entry pushes `generic_form` with a `FormModel` for that command; commands with **no** args skip the form and go straight to confirm/execute (e.g. `db.tables`, `system.info`, `command.list`).
4. Toggle `Ctrl-J` "raw JSON args": replaces the generated fields with one multi-line `Input` containing `{}`; submit passes the text as `args` after a cheap brace-balance check (escape hatch for tags/metadata values containing `,`/`=`). Server validation remains authoritative.
5. After a response: success -> `ResultViewer` (scrollable; `y` shows equivalent CLI toast); `job` response -> toast "job j-N accepted" and key `5` jumps to Jobs; error -> toast (06) with form left open.
6. `Esc` returns palette -> previous screen (router `pop`).

## Commands

```powershell
cmake --build cmake-build --target tui__11_palette_model_test tui__11_result_viewer_model_test tui__11_palette_render_test tui__11_generic_command_flow_test
ctest --test-dir cmake-build -R "^tui__11_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `11_palette_model_test.cpp` | `tui__11_palette_model_test` | unit | empty query lists all, sorted; `link.` prefix shows only link commands; `lst` subsequence matches `link.list`; unknown query -> empty list; read-only marks write/migrate disabled with reason; a synthetic extra command appears with no code change (the D9 "zero TUI code" claim); stable order for equal scores |
| `11_result_viewer_model_test.cpp` | `tui__11_result_viewer_model_test` | unit | wraps long strings at width; scroll clamps; empty JSON; 10,000-line result handled lazily |
| `11_palette_render_test.cpp` | `tui__11_palette_render_test` | unit | D9 goldens (empty, `link.`, read-only dimming); selection marker; query line `: link.[` |
| `11_generic_command_flow_test.cpp` | `tui__11_generic_command_flow_test` | unit | with Mock: choose `db.tables` -> exactly one request `{"command":"db.tables"}` and viewer shows the result; choose `link.delete`, fill slug, confirm token -> one request; cancel at confirm -> none; `job` response shows job toast; raw-JSON mode sends the typed text verbatim; read-only: Enter on a write entry sends nothing |

## Constraints

- C1/C8: the palette lists whatever `ICommandClient::commands()` returns; no command-name list in TUI code (grep test as in 07).
- Mutations pass `run_mutation` (confirm policy) (ADR 0003 rule 4).
- Layout as D9; palette is an overlay reachable from every screen via `:`.
- No blocking calls.

## Success criteria

1. Every command returned by `commands()` is selectable and executable through the palette (parameterised test over a fixture of all catalog commands incl. `link.import`, `job.cancel`).
2. D9 goldens match; the `write  (confirm)` tag appears for `link.delete`, `(jobs)` for import/export.
3. A command added to the fixture appears with a working form without TUI code changes.
4. In read-only mode no request is ever sent for a write command.

## Out of scope / risks

- Command history/favourites, argument autocompletion from live data (e.g. slug suggestions).
- Risk: if `command.list` lacks `mutating`/`supports_dry_run` flags the confirm policy will under-confirm; catalog 3.5 lists `mutating` and `dry_run`, so test against the real catalog output when 03.0 lands.
