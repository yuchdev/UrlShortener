# 34 - Coverage-gap run and review gates

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 15, 16, 17, 18-21, 24-29, 31, 32, 33 (and Tasks 01.0-06.0 complete)
**Blocks:** 08.0/01 (and through it the rest of Task 08.0)
**Estimated size:** M
**Suggested agent:** testing-expert (coverage), subtask-verifier (spec compliance), feature-reviewer + security-auditor (via /pr-review)

## Objective

Close the milestone's quality loop in the project's prescribed order
(CLAUDE.md: implement -> test -> review): run the gap analysis, fix P0/P1
gaps, verify each subtask against its spec, then run the unified review gate.
Record evidence in `status.md`.

## Dependencies

- Skills: `/test-gap [path]` (spawns testing-expert, risk-ranks: redirect/lifecycle/SSRF first; uses the `coverage` CMake target from `cmake/coverage.cmake`, lcov/genhtml, Linux), `/verify-subtask <path>` (spawns subtask-verifier, PASS/PARTIAL/FAIL matrix), `/pr-review [PR|branch]` (feature-reviewer + security-auditor in parallel, one verdict).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `docs/roadmap/0006-unified-command-model-and-tui/status.md` | modify | Task table (Tests column counts, state), evidence section |
| `docs/reviews/0006-test-gap-report.md` | create | Output of `/test-gap` (P0-P3 list, owner per item, disposition) |
| `docs/reviews/0006-pr-review.md` | create | Unified `/pr-review` verdict (written by the review agents; do not hand-edit) |
| `tests/**` | modify | Tests added to close P0/P1 gaps (new files follow numeric prefixes and `sources.cmake` registration) |

(`docs/reviews/` is the output location of the `background-reviewer` agent; create the directory if missing.)

## Classes and functions (procedure)

Ordered gate sequence; each step must be green before the next:

1. **Full local matrix** - build Release and Debug, TUI ON/OFF; run
   `ctest -L "unit|contract|integration|e2e"` plus `-L conformance` (subset) and `-L perf`. Record counts and skips in `status.md`. Any skipped test must have a named reason in this subtask's report.
2. **Flake check** - `ctest -L conformance --repeat until-fail:3` and `-R "^tui__5[0-2]_" --repeat until-fail:20`.
3. **Coverage** - Linux: `cmake -S . -B build-cov -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug && cmake --build build-cov && ctest --test-dir build-cov -L "unit|integration|conformance" && cmake --build build-cov --target coverage`. Windows has no lcov: run the Linux job (or WSL); fall back to inspection mapping (the skill documents the fallback) and say so.
4. **`/test-gap src/commands`**, **`/test-gap src/transport`**, **`/test-gap src/tui`**, **`/test-gap src/app`** (storage unification touches `src/app`/`src/composition`), **`/test-gap tests/conformance`** (meta: are corpus gaps left?). Required priority targets named by the milestone: redirect fast path (31/32), SSRF via import (20), authz matrix (19), allowlist/redaction (18), job cancel/busy (13), storage-path unification persistence (02.0), TUI confirm/read-only (28).
5. **Close gaps** - every P0 and P1 gets a test (new subtask-sized PR or in-place) or a written, reviewer-approved waiver in the report; P2/P3 are listed as follow-ups.
6. **`/verify-subtask`** for each 07.0 subtask file (34 invocations; batch by group) and for the task READMEs of 01.0-06.0 as cross-check (tests exist for every "Success criteria" item there); PARTIAL/FAIL items are fixed or recorded.
7. **`/doc-registry` and `/link-check`** on `docs/roadmap/0006-*` and `docs/testing` (new files from 29).
8. **`/pr-review`** on the milestone branch. Verdict must be LGTM; REQUEST_CHANGES items are resolved and `/pr-review` re-run. Security-auditor findings of severity CRITICAL block merge (agent contract).
9. **Sign-off** - confirm the TUI manual smoke checklist results table (`docs/testing/tui_manual_smoke.md`, executed by 06.0, traceability in 29) is filled for >= 2 terminals; update `status.md` (task states, test counts, evidence links), then hand over to 08.0.

## Implementation steps

1. Execute the sequence; paste command outputs (trimmed) into the report files.
2. For each gap decision keep a table: `ID | priority | description | disposition (test added: path | waiver: reason | follow-up: issue)`.
3. If a review demands product changes, send them back to the owning task; do not patch from this subtask.

## Commands

```bash
ctest --test-dir cmake-build -C Debug -N | tail -5                 # inventory (omit -C on single-config)
ctest --test-dir cmake-build --output-on-failure -L "unit|contract|integration|e2e"
ctest --test-dir cmake-build --output-on-failure -L conformance
ctest --test-dir cmake-build -L perf --output-on-failure
# skills (inside Claude Code): /test-gap src/commands   /verify-subtask docs/roadmap/0006-unified-command-model-and-tui/07.0-tests-and-conformance/01-corpus-schema-and-lint.md   /pr-review roadmap/tui-unified-command-model
```

## Tests

None new by itself; closes gaps found. Expected additions are recorded in `docs/reviews/0006-test-gap-report.md`.

## Constraints

- Gate order is fixed; do not run `/pr-review` before gaps are closed.
- No test is deleted or loosened to turn a gate green.
- Reports contain no secrets, DSNs or tokens (`/secret-scan` on the new report files).

## Success criteria

1. `ctest -L "unit|contract|integration|e2e"` and `-L conformance` pass with 0 failures; the skip list is documented.
2. `/test-gap` leaves no open P0; P1 items are tested or waived with approval.
3. `/verify-subtask` returns PASS for all 07.0 subtasks.
4. `/pr-review` verdict is LGTM and the report is committed.
5. `status.md` shows 07.0 complete with evidence (counts, commands, dates).

## Out of scope / risks

- Merging, tagging and release: not part of this milestone's docs.
- Risk: `/test-gap` coverage output needs Linux tooling; Windows-only developers must delegate step 3 to CI/WSL.
