# 07 - README.md update

**Parent task:** 08.0 Docs and registry sync
**State:** ⬜ Not started
**Depends on:** 03
**Blocks:** 17
**Estimated size:** M
**Suggested agent:** docs-updater

## Objective

Update the user-facing root `README.md`: build option, command API, CLI
additions, TUI, tests, repo guides - and remove the now-false statement that
CLI invocations do not share state.

## Dependencies

- Subtasks 03, 08-10 (reference docs the README links to); Task 02.0 (persistent CLI), 04.0-06.0.
- Existing headings of `README.md`: `## Build` (incl. `### Windows`), `## Run`, `## Features`, `## Storage backends`, `## API examples` (`### API notes`), `## CLI mode`, `## Core API`, `## Analytics`, `## Observability and request hardening`, `## Tests`, `## Repo guides`.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `README.md` | modify | sections below |

## Classes and functions (exact edits)

1. **`## Build`:** add a short `### Optional: terminal UI` paragraph: `-DURLSHORTENER_ENABLE_TUI=OFF` to skip FTXUI (fetched by CMake, no manual install), supported platforms, and the Windows/vcpkg note if FTXUI is taken from vcpkg (from 06.0 subtask 01 outcome).
2. **`## Run`:** after "Show all options" add `### Other modes` listing `url_shortener link <verb>`, `url_shortener cmd <name>`, `url_shortener serve-stdio`, `url_shortener tui` with one-line descriptions and a link to `docs/cli/README.md`/`docs/tui/README.md`. Verify against `url_shortener --help`.
3. **`## Features`:** add bullets: unified JSON command model (REST/CLI/stdio/TUI), jobs with progress, pair import/export, TUI.
4. **`## API examples`:** new `### Command endpoint` subsection: `POST /api/v1/commands` curl example (copy of the catalog example), `GET /api/v1/commands`, `GET /api/v1/links` (list), jobs routes; link `docs/commands/README.md` and `docs/api/README.md`. Add the new routes' names exactly as `RouteRegistry` registers them.
5. **`## CLI mode`:** (a) delete the paragraph beginning "**Note.** The CLI uses the in-memory link store ... do **not** share state" (false after Task 02.0) and replace with the persistence statement from `docs/cli/README.md` (backend chosen by storage config; in-memory backend remains per-process); (b) add `cmd` examples (`cmd link.create --json ...`, `cmd link.list`, `--raw`), `serve-stdio` one-liner, `--read-only`; (c) keep the nine-verb sentence and add "plus the generic `cmd <command>`".
6. **New `## Terminal UI`** (after CLI mode): launch line, key flags, 3-bullet feature summary (links, tables, import/export, jobs, palette), screenshot-free ASCII snippet of D1 from the golden `frame_80x24_dashboard`, link `docs/tui/README.md`, note on `--theme mono` for legacy consoles.
7. **`## Tests`:** add `ctest ... -L conformance`, `-L perf` (advisory), mention TUI tests require `URLSHORTENER_ENABLE_TUI=ON`, and `python tools/conformance_lint.py`.
8. **`## Repo guides`:** add links: `docs/commands/README.md`, `docs/commands/adding-a-transport.md`, `docs/tui/README.md`, `docs/cli/README.md` (if absent), `docs/adr/README.md`.

## Implementation steps

1. Apply edits; 2. run every shell snippet you add against the built binary (copy real output where shown); 3. `/link-check README.md`.

## Commands

```bash
cmake-build/url_shortener --help          # Windows: cmake-build\Debug\url_shortener.exe --help
cmake-build/url_shortener cmd command.list --raw
```

## Tests

None executable; link check clean.

## Constraints

- README stays user-oriented; internals link to ARCHITECTURE/ADRs.
- Examples use `example.com`; no credentials.
- Do not alter the per-platform build instructions beyond adding the TUI note.

## Success criteria

1. No sentence claims CLI state is per-process unless the in-memory backend is named.
2. Every new command in the README ran successfully against the built binary during authoring.
3. New headings: "Other modes", "Command endpoint", "Terminal UI" exist and link their references.

## Out of scope / risks

- Marketing copy, badges. Risk: README drift again; mitigated by subtask 17 spot checks.
