# 29 - TUI test plan and checklist traceability

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 22, 23, 24, 25, 26, 27, 28; 06.0/20 (`docs/testing/tui_manual_smoke.md`, already shipped), 06.0/21 (D13 Settings goldens)
**Blocks:** 34
**Estimated size:** S
**Suggested agent:** docs-writer (plan), testing-expert (traceability review)

> **Amended 2026-11-20 (as-built reconciliation).** **D13 (Settings) goldens are
> owned by 06.0/21** (`tui__21_settings_*`, `d13_*`), **not** 07.0/24-25 - fix
> the attribution here and in README exit criterion 4 (done). The traceability
> table must cite **as-built targets**: the 06.0 screen tests `tui__04`..`tui__21`
> (navigation/forms/jobs/palette/dialogs/settings are *consumed*) plus the new
> 07.0 stems `tui__22` (helpers), `tui__30` (form property), `tui__31/32/33`
> (state machine/hotkeys/non-blocking), `tui__34` (no-secrets), `tui__37`
> (preflight forward). `docs/testing/tui_manual_smoke.md` already exists.

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

`testplans/tui.md` headings: `## Scope`; `## Automated layers` (table: layer, files, target names, what it proves: mock-client helpers 22, snapshot harness 23 [consumed, 06.0/01], navigation goldens 24 [consumed, 06.0], forms/jobs/palette goldens 25 [consumed, 06.0], FormModel properties 26 [`tui__30`], state machine/hotkeys/non-blocking 27 [`tui__31/32/33`], confirm/read-only 28 [consumed `tui__06/08/18`, + `tui__34`], conformance TUI shim 10, no-listener 21); `## Manual layer` (link to `tui_manual_smoke.md`); `## Traceability` (below); `## Acceptance criteria`; `## Commands` (`ctest -R "^tui__"`, `UPDATE_GOLDEN`, `FORM_PROP_SEED`); `## Known gaps`.

Traceability table (one row per ADR 0003 rule/diagram): rows R1-R7 (rules) and D1-D13, columns: automated test (target::case) | manual step (S-id) | status (`auto`, `manual`, `both`, `gap`). Seed content:

| Item | Automated | Manual | Expected status |
|---|---|---|---|
| R1 only `ICommandClient` crosses out | `tui__02_include_boundary_test`, `commands__79_...::tui_sources_include_no_service_headers` | none | auto |
| R2 generic first | `tui__11_generic_command_flow_test`, `tui__30_form_model_property_test` | S-palette | both |
| R3 never block UI | `tui__33_nonblocking_ui_test` | S-dashboard (no freeze) | both |
| R4 mutations confirm | `tui__06_confirm_test`, `tui__08_mutation_flow_test` | S-create/delete | both |
| R5 read-only | `tui__18_readonly_matrix_test`, `tui__37_required_permission_forward_test`, `conformance_30_...` | S-readonly | both |
| R6 headless testable | all `tui__*` | none | auto |
| R7 no secrets on screen | `tui__34_no_secrets_real_registry_test`, `tui__14_tables_screen_test`, `commands__72_...` | S-tables | both |
| D1-D10 layouts | 06.0 goldens via `tui__04/06/08/10/11/12/13/14/15/16/17` (consumed; see 24, 25) | S-diagram sign-off | both |
| D13 Settings layout | 06.0/21 goldens `d13_*` via `tui__21_settings_screen_test` (consumed) | S-diagram sign-off | both |
| D11 state machine | `tui__04_router_test`, `tui__31_screen_transition_requests_test` | S-navigation | both |
| D12 same envelope on every transport | `conformance__02_tui_client_corpus` | none | auto |
| terminal restore after crash/SIGKILL | none | needs step | manual |
| resize live | none | S2 | manual |
| legacy console glyphs | none | S1 | manual |
| IME / wide characters | partial: `tui__30_form_model_property_test` P11 (JSON escaping only) | needs step | manual |

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
