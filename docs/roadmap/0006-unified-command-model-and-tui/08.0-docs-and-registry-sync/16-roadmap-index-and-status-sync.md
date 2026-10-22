# 16 - Roadmap index and milestone status sync

**Parent task:** 08.0 Docs and registry sync
**State:** ⬜ Not started
**Depends on:** 01, 02, 14, 15
**Blocks:** 17
**Estimated size:** S
**Suggested agent:** docs-updater

## Objective

Make the roadmap documents consistent with each other and with reality:
`docs/roadmap/README.md`, 0006 `plan.md` cross-references, 0006 `status.md`
and the decision log - including the subtask-number references that changed
when Tasks 07.0/08.0 were re-split.

## Dependencies

- Subtasks 01, 02, 14, 15; Task 07.0 subtask 34 (evidence for status.md).
- Current state: `docs/roadmap/README.md` already has the 0006 row, "All six milestones..." paragraph and an "Execution order" section; 0006 `plan.md` section "Impact on other milestones" already says "(applied in Task 08.0 subtasks 14-15)" and the 0004 bullet already carries the REST-adapter rate-limit wording (open decision D5); `decision-log.md` already has the section "Open decisions needing human review" (D1-D16, extended from D1-D12 by the second spec review); a 2026-10-22 human-review pass confirmed D1-D16 as-is (see decision-log.md "Human review (2026-10-22)") - this subtask's job is to propagate that into `status.md`'s Open risks and (per item 4 below) formally record each outcome in the decision log's own table rows; `status.md` task table is all `⬜ Not started`.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `docs/roadmap/README.md` | modify | row status text, paragraph, execution order |
| `docs/roadmap/0006-unified-command-model-and-tui/plan.md` | modify | cross-references, impact bullet 4, file map (real paths) |
| `docs/roadmap/0006-unified-command-model-and-tui/status.md` | modify | task table, gate status, risks closed |
| `docs/roadmap/0006-unified-command-model-and-tui/decision-log.md` | modify | add entries from this task |
| `docs/roadmap/0002-admin_console/status.md`, `0004-web-ui/status.md` | verify | edits from 14/15 present |

## Classes and functions (exact edits)

1. **`docs/roadmap/README.md`:**
   - Milestones table, rows 0002 and 0004 Spec column: append " - requires 0006 first" (0004: "- soft dependency on 0006").
   - Paragraph under the table: when 0006 is complete change "Milestones 0002, 0004, 0005 and 0006 are fully specified but **not started**" to list 0006 as implemented (with date) and keep the others not started.
   - "Execution order": keep; add "0006 completed <date>" once true.
   - Add under "Linking convention" nothing; verify it still reflects the template-path rule.
2. **0006 `plan.md`:**
   - Verify the heading says `(applied in Task 08.0 subtasks 14-15)` (already done on the design branch).
   - Impact bullet for 0004: keep the REST-adapter wording if open decision D5 was confirmed, otherwise replace it with the decision taken.
   - Tasks table row 08.0 Output: add "TUI guide, ARCHITECTURE/CLAUDE/README sync".
   - File map block: replace the sketch with the verified tree (`git ls-files` for the directories listed), including `tests/conformance/{commands,support,runners,py}`, `docs/tui/`.
   - Task table links for 07.0/08.0 stay valid (directory names unchanged).
3. **0006 `status.md`:** set each task row to its true state with test counts from `ctest -N`/the 07.0 subtask 34 report; update "Current gate status" with dates and links to `docs/reviews/0006-test-gap-report.md` and `0006-pr-review.md`; move closed risks to Notes with resolution; keep unresolved ones.
4. **`decision-log.md`:** the human outcome of every row of "Open decisions needing human review" (D1 link ids, D2 `ReplaceLinkTarget`, D3 JSON scanner, D4 command-API exposure, D5 rate-limit placement, D6 ADR acceptance, D7 adapters, D8 CTest label/`PERF_SOURCES`, D9 catalog generator scope, D10 ArgSpec caps, D11 CLI flag grammar, D12 write-behind, D13 job dispatch, D14 audit-sink destinations, D15 `serve-stdio` file-path default, D16 argument-dependent permission) was already recorded on 2026-10-22 ("Human review" section: all sixteen confirmed, none reversed) - verify it is still present and add a row Q22 for the ARCHITECTURE.md dangling ADR repair (subtask 05). Same columns as existing.
5. Verify that no document still cites "Task 07.0 subtask 0N" or "Task 08.0 subtask 0N" with old meanings: `git grep -n "07.0 subtask\|08.0 subtask" -- docs`.

## Implementation steps

1. Apply edits; 2. `/doc-xref docs/roadmap/0006-unified-command-model-and-tui/08.0-docs-and-registry-sync` and `.../07.0-tests-and-conformance` to catch stale subtask-number references from Tasks 01.0-06.0 docs; 3. `/link-check docs/roadmap`.

## Commands

```bash
git grep -n "subtask 0[0-9]" -- docs/roadmap/0006-unified-command-model-and-tui
```

## Tests

None executable.

## Constraints

- Status values only from the vocabulary `✅ Complete · 🔶 In progress / partial · ⬜ Not started`.
- Evidence must exist (commands run, files) before marking anything complete.

## Success criteria

1. No stale subtask-number reference remains.
2. `docs/roadmap/README.md` and 0006 `status.md` agree on the milestone state.
3. Five decision-log entries added.

## Out of scope / risks

- Marking 0002/0004 states. Risk: premature "complete" claims; status changes only after subtask 34 evidence.
