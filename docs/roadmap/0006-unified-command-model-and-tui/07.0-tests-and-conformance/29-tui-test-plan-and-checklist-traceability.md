# 29 - TUI test plan and checklist traceability

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 22, 23, 24, 25, 26, 27, 28; 06.0/20 (writes the manual smoke checklist)
**Blocks:** 34
**Estimated size:** S
**Suggested agent:** docs-writer (plan), testing-expert (traceability review)

## Objective

Avoid duplicating Task 06.0's manual checklist: **06.0 owns and executes**
`docs/testing/tui_manual_smoke.md` (real terminals, resize, legacy console,
tmux, persistence across restarts, C5 checks, D1-D13 sign-off). This subtask
creates the missing glue: a TUI test plan that maps every automated test of
group E to what it proves, and a traceability table showing which behaviors are
covered only manually, so no gap is accidental.

## Dependencies

- 06.0 manual smoke checklist subtask (creates `docs/testing/tui_manual_smoke.md`; its steps S1..Sn include start/quit, min size, navigation, typing vs hotkeys, dashboard, links, create/edit/delete, detail/stats, tables, import/export, ...).
- Test plan style: `docs/testing/testplans/*.md` (Scope, Required coverage, Acceptance criteria, E2E analogs, Commands).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `docs/testing/testplans/tui.md` | create | TUI test plan + traceability table |
| `docs/testing/tui_manual_smoke.md` | modify (additive rows only, with 06.0 owner agreement) | add steps for items automated tests cannot cover and the checklist lacks (list below) |
| `docs/testing/README.md` | modify | not here: index edits are 08.0 subtask 12 |

(The brief named `docs/test/`; this repo uses `docs/testing/`.)

## Classes and functions (document structure)

`testplans/tui.md` headings: `## Scope`; `## Automated layers` (table: layer, files, target names, what it proves: mock client 22, snapshot harness 23, navigation goldens 24, forms/jobs/palette goldens 25, FormBuilder properties 26, state machine and non-blocking 27, confirm/read-only 28, conformance TUI shim 10, no-listener 21); `## Manual layer` (link to `tui_manual_smoke.md`); `## Traceability` (below); `## Acceptance criteria`; `## Commands` (`ctest -R "^tui__"`, `UPDATE_GOLDEN`, `FORM_PROP_SEED`); `## Known gaps`.

Traceability table (one row per ADR 0003 rule/diagram): rows R1-R7 (rules) and D1-D13, columns: automated test (target::case) | manual step (S-id) | status (`auto`, `manual`, `both`, `gap`). Seed content:

| Item | Automated | Manual | Expected status |
|---|---|---|---|
| R1 only `ICommandClient` crosses out | `tui__02_include_boundary_test`, `commands__79_...::tui_sources_include_no_service_headers` | none | auto |
| R2 generic first | `tui__33_...::palette_new_command_zero_tui_code`, `tui__40_...` | S-palette | both |
| R3 never block UI | `tui__52_...` | S-dashboard (no freeze) | both |
| R4 mutations confirm | `tui__60_...` | S-create/delete | both |
| R5 read-only | `tui__61_...`, `conformance_30_...` | S-readonly | both |
| R6 headless testable | all `tui__*` | none | auto |
| R7 no secrets on screen | `tui__62_...`, `commands__72_...` | S-tables | both |
| D1-D10, D13 layouts | goldens (24, 25) | S-diagram sign-off | both |
| D11 state machine | `tui__50_...` | S-navigation | both |
| D12 same envelope on every transport | `conformance__02_tui_client_corpus` | none | auto |
| terminal restore after crash/SIGKILL | none | needs step | manual |
| resize live | none | S2 | manual |
| legacy console glyphs | none | S1 | manual |
| IME / wide characters | partial: `tui__40_` P11 (JSON escaping only) | needs step | manual |

Steps to add to `tui_manual_smoke.md` if absent (additive rows, numbered after the existing ones): kill the process hard and check terminal state; paste a 1 MB payload in import; every hotkey from the D1/D3/D6/D8 footers once (table); read-only banner and disabled actions; `--yes` behavior; wide-character slug display; 10k-row table responsiveness.

## Implementation steps

1. Read the merged 06.0 checklist; fill the traceability table with real S-ids and real test names (`ctest -N`).
2. Mark every `gap` row with an owner and either a new automated test (add to subtask 34's gap report) or a manual step.
3. Run `/link-check docs/testing`.

## Commands

```bash
ctest --test-dir cmake-build -N -R "^tui__"
```

## Tests

None executable.

## Constraints

- Does not rewrite 06.0's checklist; additive rows only, noted in the PR.
- Test names in the plan must exist.

## Success criteria

1. `testplans/tui.md` exists with the sections above and a traceability row for R1-R7 and D1-D13.
2. No row has status `gap` without an owner.
3. Every cited target appears in `ctest -N`.

## Out of scope / risks

- Executing the manual checklist and recording results: owned by 06.0, re-verified at 34 step 9.
- Risk: the 06.0 checklist file name or step ids change; update this plan last.
