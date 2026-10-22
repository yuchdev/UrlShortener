# 10 - TUI user guide

**Parent task:** 08.0 Docs and registry sync
**State:** ⬜ Not started
**Depends on:** Task 06.0 (shipped TUI), Task 07.0 subtasks 24-25 (goldens) and Task 06.0's manual smoke checklist
**Blocks:** 11, 13, 17
**Estimated size:** M
**Suggested agent:** docs-writer

## Objective

Create `docs/tui/README.md`, the operator's guide to `url_shortener tui`:
launching, screens, hotkeys, import/export formats, jobs, read-only mode, and
the relationship to `cmd` (every screen shows its equivalent command).

## Dependencies

- ADR 0003 (D1-D13, rules), catalog (`link.import` CSV/NDJSON), goldens in `tests/unit/tui/golden/` (source of truthful screen text), `url_shortener tui --help`.
- `docs/tui/` does not exist yet (new directory).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `docs/tui/README.md` | create | the guide |

## Classes and functions (headings to write)

1. `# Terminal UI` - one paragraph: what it is, no listener, same commands as REST/CLI (ADR 0002/0003 links).
2. `## Requirements` - terminal 80x24, UTF-8 recommended, `--theme mono` for legacy consoles, build option `URLSHORTENER_ENABLE_TUI`, where binaries without TUI report the subcommand as unavailable (verify real message).
3. `## Starting` - synopsis + flag table (`--storage-config`, `--read-only`, `--yes`, `--theme dark|light|mono`; others from `--help`), in-memory banner meaning, which backend to use for persistence, relation to server mode (can run beside a server on the same SQLite file? state the verified answer from 02.0 or "not supported/untested").
4. `## Screen map` - table: key, screen, commands used (from ADR D1 tab bar: Dashboard, Links, Tables, Import/Export, Jobs, Analytics?, Palette); note Analytics tab status as built (ADR 0003 D1 lists `[6]Analytics`; plan says deep analytics belong to 0002 - state what the tab does or that it is absent).
5. `## Navigating` - global hotkeys table copied from the footer strings of the goldens; focus rules; Esc/q semantics; Ctrl-C.
6. `## Links` - filter syntax (`camp:` prefix in D3 header: verify what filter grammar exists), paging, row actions, detail/stats/preview, create/edit form fields and how `tags`, `metadata` (`k=v`), `campaign` are typed, expiry format, validation messages = server error codes; "Equivalent" footer.
7. `## Tables` - allowlisted tables, sorting, search, horizontal scroll, row JSON; why auth/secret tables are absent.
8. `## Import and export` - CSV (`full_url,slug`, blank slug generated), NDJSON (`link.create` args per line), `on_conflict` options, dry run, file vs paste, size/row limits as built, export formats/destinations, CSV-injection note as implemented; example files in a fenced block.
9. `## Jobs` - states, progress, cancel, auto-refresh, retention (in-memory; lost on exit).
10. `## Command palette` - `:`; filtering; generic forms for any command; confirm dialogs and typed confirmation; `--yes`.
11. `## Read-only mode` - what is disabled, what the reason line means.
12. `## Errors` - toast anatomy, `d` details, table mapping of common codes (`slug_conflict`, `invalid_url`, `not_authorized`, `busy`, `storage_failure`) to what to do.
13. `## Troubleshooting` - terminal garbled (try `--theme mono`, Windows Terminal), colors, resize, tmux, "terminal too small", locked SQLite, stuck job.
14. `## Security` - local operator trust model, secrets never shown, `path` access, no network exposure.
15. `## See also` - catalog, CLI reference, ADR 0003, development guide (subtask 11), manual checklist.

## Implementation steps

1. Run the TUI against a SQLite temp store and verify every statement (use `docs/testing/tui_manual_smoke.md` as the script); 2. paste text from goldens for hotkey tables to avoid drift; 3. mark anything not verifiable as "not verified" or remove it; 4. `/link-check docs/tui`.

## Commands

```bash
cmake-build/url_shortener tui --storage-config tmp/links.yaml --theme mono
cmake-build/url_shortener tui --help
```

## Tests

None executable; the manual smoke checklist (`docs/testing/tui_manual_smoke.md`) validates the guide's claims.

## Constraints

- Operator-facing language; internals live in the development guide.
- No screenshots (binary assets); ASCII frames only, taken from goldens.
- Relative links inside `docs/` (local convention).

## Success criteria

1. All fifteen headings exist.
2. Hotkey table equals the hotkey table object in the code (spot-check 5 keys) and the footers in goldens.
3. Every flag in "Starting" appears in `tui --help`.

## Out of scope / risks

- Localization, accessibility statement. Risk: the guide is written before the final key map; this subtask runs after 06.0 stabilizes.
