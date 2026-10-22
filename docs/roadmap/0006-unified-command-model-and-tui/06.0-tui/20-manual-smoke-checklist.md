# 20 - Manual smoke checklist and diagram sign-off

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/01-19 (all); Task 02.0 (persistent backend for the restart check); 05.0/08, 05.0/10 (import/export)
**Blocks:** 07.0/29 (checklist traceability), 08.0/10 (TUI user guide quotes this checklist)
**Estimated size:** S
**Suggested agent:** testing-expert, docs-writer

## Objective

Write and execute the manual smoke checklist for what snapshot tests cannot
prove: real terminals, key handling, resize, legacy Windows console, tmux,
persistence across restarts, and contract C5 (no listener, no `uri.txt`). The
checklist is also the sign-off that each diagram **D1-D12** of
[ADR 0003](/docs/adr/0003-tui-framework-and-architecture.md) was seen
rendering as specified (or the ADR was amended). Repo convention: the testing
agent writes manual checklists to `docs/test/<feature>.md`, but this repository
keeps them in `docs/testing/` (see `docs/testing/cli_link_commands.md`); use
**`docs/testing/tui_manual_smoke.md`** (flagged in the task README).

## Dependencies

- Everything in 06.0; real backend config from Task 02.0 (SQLite file recommended); a seed script/commands to create ~30 links (use `url_shortener cmd link.create` from Task 04.0, or the TUI import).
- Tools: Windows Terminal, legacy `conhost` (run `cmd.exe` from Start), Linux terminal or WSL, optional `tmux`; PowerShell `Get-NetTCPConnection`.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `docs/testing/tui_manual_smoke.md` | create | the checklist below as numbered steps with expected results and a results table (date, terminal, build hash, pass/fail, notes) |
| `docs/testing/README.md` | modify | one-line index entry |
| `docs/adr/0003-tui-framework-and-architecture.md` | modify (only if a diagram needed amending) | list amendments with the reason |

## Classes and functions

None (documentation). Required checklist content (each item: steps, expected, result column):

| # | Area | Steps (abridged) | Expected |
|---|---|---|---|
| S1 | Start/quit (D1) | `url_shortener tui`; press `Ctrl-C`; run in Windows Terminal, conhost, Linux, tmux | frame like D1; clean exit code 0; shell prompt, cursor and scrollback intact |
| S2 | Min size (D1) | resize to 79x23, then 80x24, then 140x40 | "terminal too small" below 80x24; reflow above; no crash |
| S3 | Navigation (D11) | keys `1`..`6`, `:`; `q` back from nested screens | transitions match D11; Analytics shows placeholder |
| S4 | Typing vs hotkeys | focus slug field, type `1 2 : q` | characters appear in the field; no navigation |
| S5 | Dashboard (D2) | open with seeded data | counts match `link.list` totals; sampled labels visible; no freeze while loading |
| S6 | Links (D3) | filter `camp:x`, status radio, page with PgDn, sort | table updates; footer `page N/M`; selection stays visible |
| S7 | Create/edit/delete (D5, D10) | `n` create; duplicate slug; `e` edit; `t` toggle; `d` with wrong then right slug; `u` restore | toasts show exact error code (`slug_conflict`); delete requires typed slug; `Equivalent:` footer matches the request |
| S8 | Detail/stats (D4) | Enter on a link with traffic (curl `GET /slug` against a running server on the same SQLite file in a second terminal) | stats strip shows counts after refresh `r` |
| S9 | Tables (D6) | open Tables, browse `links`, `click_events`, `jobs`; search; Enter row JSON; `n` on `links` | no auth/secret tables; insert only on `links`; no hash/token columns other than `client_id_hash` |
| S10 | Import/export (D7, D8) | paste 3 lines (one blank slug), dry run, then real; skip/fail/overwrite conflict; export CSV | job appears in Jobs with progress; final counts match; created links visible in Links |
| S11 | Jobs (D8) | cancel a running large import (>= 5,000 lines) | state `cancelled`; partial effects reported; polling stops when idle (CPU near 0) |
| S12 | Palette (D9) | `:` type `link.`; run `system.info`, `command.list`, a write command | every catalog command listed; results shown in viewer |
| S13 | Read-only | `tui --read-only` | badge `READ-ONLY`; write controls dim with reason; palette write entries dim; attempting them sends nothing (verify with server audit/Jobs list) |
| S14 | Mono/ASCII | `tui --theme mono` in conhost with raster font | only ASCII glyphs, layout intact |
| S15 | Slow backend | point at a slow/unavailable Postgres or add sleep via test backend | UI stays responsive (keys echo, can quit); timeouts shown as toasts |
| S16 | C5 (no listener) | PowerShell snippet from [19](/docs/roadmap/0006-unified-command-model-and-tui/06.0-tui/19-tui-cli-mode-wiring.md) | no listening port for the process; no `uri.txt` in cwd |
| S17 | Persistence | create links, quit, rerun `tui` with same `--storage-config` | links still there; with in-memory backend banner "memory store - data is lost on exit" shown |
| S18 | Non-TTY | `url_shortener tui < NUL` and `... | more` | exit 2 with a one-line message, no garbage |
| S19 | OFF build | build with `-DURLSHORTENER_ENABLE_TUI=OFF`, run `tui` | exit 2, "built without TUI support" |
| S20 | Unicode | slug/URL with non-ASCII, wide CJK in tags | alignment holds; no truncated multibyte garbage |

## Implementation steps

1. Draft `docs/testing/tui_manual_smoke.md` from the table with exact commands per OS (PowerShell and bash variants).
2. Execute on at least: Windows Terminal, legacy conhost, one Linux terminal; one `tmux` pass for S14.
3. Record failures as issues; fix in the owning subtask; amend ADR 0003 diagrams where the implementation intentionally differs (list in the doc).
4. Attach a coverage matrix: D1-D12 -> checklist item -> tester initials/date.

## Commands

```powershell
cmake --build cmake-build --target url_shortener
.\cmake-build\url_shortener.exe tui --storage-config .\config\storage.sqlite.yaml   # flag name per Task 02.0
Get-NetTCPConnection -State Listen | Where-Object OwningProcess -eq (Get-Process url_shortener).Id
ctest --test-dir cmake-build -L unit -R "^tui__" --output-on-failure   # automated suite must be green first
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `docs/testing/tui_manual_smoke.md` (manual) | none | manual | S1-S20 above, executed per terminal |
| (automated gate) | `ctest -L unit -R "^tui__"` | unit | all earlier subtasks' tests green before the manual pass |

## Constraints

- Runs against a disposable data directory; never against production data.
- Findings that contradict an ADR 0003 diagram amend the ADR in the same change set (ADR rule: "deviations amend the ADR").
- Checklist must not include secrets (DSNs/passwords) in recorded output; redact before pasting logs.

## Success criteria

1. All S1-S20 pass on Windows Terminal and one Linux terminal; conhost and tmux results recorded (known limitations listed, not hidden).
2. The D1-D12 coverage matrix is complete (every diagram has at least one passing manual item or an explicit "snapshot-only" justification for D11/D12).
3. S16 confirms C5 on the shipped binary.
4. `docs/testing/tui_manual_smoke.md` exists and is linked from `docs/testing/README.md`.

## Out of scope / risks

- Automated pty-driven end-to-end tests (could be added later with `expect`/ConPTY harness).
- Performance benchmarking of redraw rates, accessibility/screen-reader support.
- Risk: legacy conhost may render FTXUI badly; the supported matrix is whatever passes S14, and the TUI user guide (08.0/10) must say so.
