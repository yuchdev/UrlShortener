# 15 - Import / export pairs screen (D7)

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/03, 04, 06, 08, 11; 05.0/05 (`job.*`), 05.0/08 (`link.import`), 05.0/10 (`link.export`)
**Blocks:** 06.0/18, 20 (no compile-time coupling to the Jobs screen: the hand-off is the string `ScreenContext::focus_job_id` declared in the app shell, which the Jobs screen consumes in `on_enter`)
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Render **D7**: the "Import pairs (command: link.import, runs as a job)" form with
source (paste / file path), format (CSV `full_url,slug` / NDJSON), on-conflict
(skip / fail / overwrite), a multi-line paste area, `[x] dry run (validate only)`,
`[ Start import ]  [ Cancel ]`, and the follow-up behaviour "after Start -> Jobs
screen". Also provides the export counterpart (`link.export`: format, filters,
optional path). Implements D11 edge `ImportExport -> Jobs (Start, job accepted)`.
This is the user-facing way to insert many `full_URL -> short_URL` pairs.

## Dependencies

- Internal: 06.0/08 (`run_mutation`, field widgets), 06 (confirm/toast), 11 (result viewer), 03 (`AsyncCaller`), 04 (`Router`).
- Commands (catalog 3.3): `link.import` args `format` (`csv|ndjson`), `data` XOR `path`, `on_conflict` (`skip|fail|overwrite`; default `skip`), option `dry_run`; `link.export` args `format`, `status`, `q`, `tag`, `campaign`, `path`. Job envelope `{"ok":true,"job":{"id":"j-9","state":"queued"}}`.
- ADR 0003 D7 now shows the catalog values `full_url,slug` and `overwrite` (amended on the design branch); `overwrite` exists only if open decision D2 (Task 05.0 subtask 09) is confirmed - the radio is generated from the `on_conflict` enum, so it disappears automatically if the value is dropped.
- A read-only session cannot export to a `path` (`not_authorized`, catalog 3.3); the export path field renders disabled in read-only mode (subtask 18).
- Limit source: inline payload size <= `--max-request-body-bytes` (default 65536). In-process TUI passes the configured limit through `TuiOptions.max_inline_bytes` (field defined in subtask 19, default 65536); larger input must use `path`.
- External FTXUI: `Input` with `InputOption::multiline = true` for the paste area (VERIFY AT PIN TIME), `Radiobox`, `Checkbox`, `Button`, `Container::Vertical/Horizontal`, `yframe`, `Tab` for Import/Export sub-tabs.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/tui/import_model.hpp` / `src/tui/import_model.cpp` | create | `ImportModel`, `ExportModel`, CSV/NDJSON line pre-checks (model lib) |
| `src/tui/screens/import_export_screen.cpp` | create | `ImportExportScreen : IScreen` (UI lib) |
| `tests/unit/tui/15_import_model_test.cpp`, `15_export_model_test.cpp`, `15_import_export_screen_test.cpp` | create | see Tests |
| `tests/unit/tui/golden/d07_import_80x24.txt`, `d07_import_file_80x24.txt`, `d07_import_toolarge_80x24.txt`, `d07_export_80x24.txt` | create | goldens |
| `sources.cmake` | modify | register |

## Classes and functions

```cpp
enum class ImportSource { paste, file };   enum class ImportFormat { csv, ndjson };
enum class OnConflict { skip, fail, overwrite };
class ImportModel {                                          // NEW
public:
    explicit ImportModel(std::size_t max_inline_bytes);
    void set_source(ImportSource);  void set_format(ImportFormat);  void set_on_conflict(OnConflict);
    void set_text(std::string);  void set_path(std::string);  void set_dry_run(bool);
    struct Preview { std::size_t lines = 0, blank_slugs = 0; std::vector<std::string> warnings; };
    Preview preview() const;                                 // advisory: counts lines, flags lines without a comma (csv), blank-slug count (D7 "<- blank slug = generated")
    std::optional<std::string> blocking_error() const;       // empty payload, > max_inline_bytes, empty path
    commands::CommandRequest to_request(const std::string& request_id) const;   // args: format, data|path, on_conflict; options.dry_run
};
class ExportModel {
public:
    void set_format(ImportFormat);  void set_filter(const LinkListFilter&);  void set_path(std::string);
    commands::CommandRequest to_request(const std::string& request_id) const;
};
class ImportExportScreen final : public IScreen { /* id() == ScreenId::import_export */ };
```

## Implementation steps

1. Two sub-tabs: `Import` (default, D7) and `Export`; `Tab`/`Shift-Tab` moves focus, `F2` switches sub-tab.
2. Paste area shows placeholder with the D7 sample lines when empty; live `Preview` line "3 lines, 1 blank slug (will be generated)" under the area.
3. `[ Start import ]`: `blocking_error()` -> inline message, no request; otherwise `run_mutation(link.import)` (mutating, so confirm yes/no unless `--yes`; with `dry_run` checked the confirm text says "validate only" and does not require confirm because nothing mutates - policy still treats it as mutating in v1, simpler and safer; documented).
4. On `job` response: toast "job j-9 accepted", `Router::push(jobs)` and tell the Jobs screen to focus `j-9` via `ScreenContext.focus_job_id` (D7 footer arrow: "after Start -> Jobs screen").
5. A non-job response (e.g. `unsupported` for `path` on a remote client, `invalid_field`) -> toast; form data preserved.
6. Export: `[ Start export ]` -> same job flow; Jobs detail later shows `rows` and `path`/inline `data`. Inline `data` result > 64 KiB is displayed truncated with the line count and a hint to use `path`.
7. File-path source: only a text field (no file browser); path existence is not checked client-side (server validates; avoids TOCTOU and keeps TUI free of filesystem logic).

## Commands

```powershell
cmake --build cmake-build --target tui__15_import_model_test tui__15_export_model_test tui__15_import_export_screen_test
ctest --test-dir cmake-build -R "^tui__15_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `15_import_model_test.cpp` | `tui__15_import_model_test` | unit | `to_request` bytes golden for CSV paste, NDJSON paste (newlines/quotes escaped inside `data`), file path (no `data` key), dry run (`options.dry_run:true`); boundary: payload exactly `max_inline_bytes` OK, +1 blocked; empty; CRLF line endings preserved; line without comma flagged; trailing newline; blank slug counted; unicode URLs |
| `15_export_model_test.cpp` | `tui__15_export_model_test` | unit | formats; filters map to `status/q/tag/campaign`; path optional |
| `15_import_export_screen_test.cpp` | `tui__15_import_export_screen_test` | unit | D7 golden; typing into the paste area then Start (confirm) sends one `link.import`; job response -> router at `jobs` with focus id `j-9`; `invalid_field` response keeps data; `--yes` skips confirm; read-only disables Start with reason and sends nothing; oversize shows blocking message and no request |

Manual: paste 3 lines incl. blank slug, run with dry-run, then for real; check Jobs and the Links screen.

## Constraints

- Import rows are processed by `link.import` -> `CreateLink` server-side (C1); the TUI does no URL/SSRF validation of its own (preview is syntactic only).
- `path` is a local-operator capability; the TUI never opens or reads the file itself.
- `overwrite` conflict mode requires an explicit radio selection (never default) and its label includes "destructive".
- Layout per D7 at 80 columns; multi-line `Input` must scroll (`yframe`).

## Success criteria

1. `d07_import_80x24.txt` matches D7's structure.
2. The request sent equals `ImportModel::to_request` golden bytes (mock asserts).
3. After an accepted job the screen shown is Jobs with the job focused.
4. Inline payload above the limit never reaches `call`.

## Out of scope / risks

- File picker dialogs, progress inside this screen (Jobs owns progress), export download/save-as, drag-and-drop.
- Risk: pasting megabytes into an FTXUI `Input` is slow; cap the buffer at `max_inline_bytes + 1` and show the oversize message.
