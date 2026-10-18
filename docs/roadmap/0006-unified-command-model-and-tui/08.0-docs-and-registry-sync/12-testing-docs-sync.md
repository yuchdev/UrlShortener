# 12 - Testing docs sync

**Parent task:** 08.0 Docs and registry sync
**State:** ⬜ Not started
**Depends on:** Task 07.0 (all subtasks merged)
**Blocks:** 13, 17
**Estimated size:** M
**Suggested agent:** docs-updater

## Objective

Update the project test plan and test plans so they describe the conformance
corpus, TUI tests, security additions, perf registration and the persistence
change, and remove statements Task 02.0 made false (the CLI "empty in-memory
store per invocation" assumption). The task brief names `docs/test/`; the
repo's test docs are in **`docs/testing/`**.

## Dependencies

- Task 07.0 README "Conventions" table (lists, targets, labels); Task 06.0's manual smoke checklist subtask (creates and owns `docs/testing/tui_manual_smoke.md`) and 07.0 subtask 29 (creates `testplans/tui.md`).
- Existing structure of `docs/testing/README.md`: sections 1-12 (3.1-3.5 categories, 4 directory map, 5 traceability, 6 CLI plan, 7 REST, 8 storage, 9 analytics, 10 security, 11 commands, 12 adding tests).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `docs/testing/README.md` | modify | sections below |
| `docs/testing/testplans/conformance.md` | create | cross-transport conformance plan |
| `docs/testing/testplans/cli.md` | modify | remove "empty in-memory store per invocation" and Storage note (per 02.0 outcome); add `cmd`/`serve-stdio` coverage lines |
| `docs/testing/testplans/security.md` | modify | add the 07.0 security items |
| `docs/testing/testplans/e2e.md` | modify | add section `19_cmd_serve_stdio_no_listener` |
| `docs/testing/testplans/integration.md`, `unit.md` | modify | one-line pointers to conformance and `commands__`/`tui__` suites |
| `docs/testing/cli_link_commands.md` | modify | the "Storage scoping" section (it says `linkRepository()` is process-local and cross-invocation steps cannot be tested) - rewrite per 02.0 outcome; check first whether 02.0's docs-sync subtask already did |
| `docs/testing/e2e_testing/sections/19_cmd_serve_stdio_no_listener.md` | create | section doc in the style of `13_cli_no_server_socket.md` (sections 14-18 have no docs; note the pre-existing gap, optionally backfill) |
| `docs/testing/testplans/tui.md`, `docs/testing/tui_manual_smoke.md` | verify | created by 07.0/29 and 06.0; link from index only (06.0 already adds a one-line index entry to `docs/testing/README.md`: do not duplicate) |

## Classes and functions (exact edits to `docs/testing/README.md`)

1. **Section 1 Purpose:** "two transport modes" -> add the command model and TUI: server mode, CLI/stdio/TUI local modes, all over the command registry.
2. **Section 2 Quality objectives:** add rows: "Keep transports identical" (conformance corpus across six adapters, drift gates), "Protect the redirect fast path" (structural guard + advisory perf), "Keep TUI headless-testable" (mock client + goldens).
3. **Section 3 Test categories:** add `### 3.6 Conformance tests` (label `integration;conformance`, location `tests/conformance/`, scope, acceptance: identical `result`/`error.code` across transports) and extend 3.1 examples with command registry and TUI; extend 3.5 (perf now registered; guard is advisory; structural guard is a unit gate).
4. **Section 4 Directory map:** add `unit/commands/`, `unit/tui/` (+ `golden/`), `conformance/` (commands, support, runners, py), `perf/` (analytics, redirect); `tools/conformance_*.py`.
5. **Section 5 traceability:** add rows mapping manual scenarios (TUI checklist items, cmd/stdio scenarios) to automated analogs.
6. **Section 6 CLI test plan:** rewrite storage statements; add `cmd`, `serve-stdio`, `tui` bullets; link to conformance.
7. **Section 10 Security plan:** add allowlist/redaction, authz matrix, import SSRF/size/path, no-listener items with test names.
8. **Section 11 commands:** add `-L conformance`, `-L perf`, `UPDATE_GOLDEN`, `FORM_PROP_SEED`, `python tools/conformance_lint.py`; keep the Windows `-C Debug` note and the POSIX-only e2e note.
9. **Section 12 Adding tests:** add the four recipes (new command -> corpus case first; new transport -> shim; new TUI screen -> snapshot + state-machine row; new `ArgSpec` type -> FormBuilder property test).
10. **Companion documents list:** add the conformance plan, TUI plan, TUI checklist.

`testplans/conformance.md` structure: Scope; Corpus format (link `tests/conformance/commands/README.md`); Adapters table (id, how run, caps, skip policy); Gates (lint, coverage, catalog drift, route drift); Commands; Acceptance; E2E analogs.

## Implementation steps

1. Apply edits in file order; 2. verify each test name with `ctest -N`; 3. `/link-check docs/testing`.

## Commands

```bash
ctest --test-dir cmake-build -N | findstr /i "conformance tui__ commands__ docs__ perf analytics__"     # bash: grep -iE
```

## Tests

None executable; link check clean.

## Constraints

- Relative links (local convention in `docs/testing`).
- Do not delete historical e2e analog tables; mark changed rows.

## Success criteria

1. Every test name cited exists.
2. No testing doc still claims CLI invocations never share state without the in-memory qualifier.
3. New plans linked from the README companion list.

## Out of scope / risks

- Backfilling docs for e2e sections 14-18 (optional, noted). Risk: overlap with the CLI docs edits in subtask 08 - this subtask edits only `docs/testing/**`.
