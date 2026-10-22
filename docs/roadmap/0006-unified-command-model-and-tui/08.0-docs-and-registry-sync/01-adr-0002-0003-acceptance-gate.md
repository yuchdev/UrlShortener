# 01 - ADR 0002/0003 acceptance gate

**Parent task:** 08.0 Docs and registry sync
**State:** ⬜ Not started
**Depends on:** Task 07.0 subtask 34 (all gates passed); explicit user instruction to accept
**Blocks:** 02, 16
**Estimated size:** S
**Suggested agent:** docs-updater (edit), app-architect (reviews the "as built" deltas)

## Objective

Move ADR 0002 and ADR 0003 from `Proposed` to their final status after the
decisions open in the decision log are closed, and amend the ADR text where
the implementation deviated, so the ADRs describe what was built.

## Dependencies

- [decision-log.md](/docs/roadmap/0006-unified-command-model-and-tui/decision-log.md): Q1 (FTXUI), Q3 (envelope), Q6 (trust model), Q17 (ADR status), Q18 (`target_url` vs `url`), "things to research" 1-2 (FTXUI facts, adoption claim).
- Status vocabulary: `Proposed | Accepted | Implemented | Superseded | Deprecated` ([docs/adr/README.md](/docs/adr/README.md)).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `docs/adr/0002-unified-command-model.md` | modify | header `Status`, new `Date accepted`, "As built" amendments |
| `docs/adr/0003-tui-framework-and-architecture.md` | modify | header `Status`, FTXUI version pin, diagram deltas |
| `docs/roadmap/0006-unified-command-model-and-tui/decision-log.md` | modify | mark Q1, Q17, Q18 resolved; strike items researched |

## Classes and functions (exact edits)

**ADR 0002** (`docs/adr/0002-unified-command-model.md`):

1. Line 3 `> **Status:** Proposed` -> `> **Status:** Accepted` (or `Implemented` if the milestone branch is merged when this runs; use `Implemented` only after merge). Add under it `> **Accepted:** <YYYY-MM-DD> (decision-log Q17)`.
2. `## Consequences` -> `### Negative`, third bullet ("The ADR 0001 storage-path duality ... must be resolved in this milestone (Task 02.0)"): rewrite in past tense as resolved by Task 02.0 (Task 02.0's docs-sync subtask asks 08.0 to do this ADR 0002 wording; it edits only ADR 0001 and `docs/cli`). Verify against the merged 02.0 behavior.
3. Under `## Decision` add a short `### As built` list (only if deviations exist), one bullet per deviation with the owning task, e.g. final names of `ITransportAdapter`, `CallerContext`, job `busy` handling, `target_url` alias handling (Q18 outcome), error codes added/removed versus the catalog.
4. `## Validation / Rollout`: replace the future-tense sentence with the actual stage order and link the status file (`/docs/roadmap/0006-unified-command-model-and-tui/status.md`).
5. `## Links`: add the link to `docs/commands/adding-a-transport.md` (use the link style of the file) and `tests/conformance/commands/README.md` (as a code span: it is outside `docs/`).

**ADR 0003** (`docs/adr/0003-tui-framework-and-architecture.md`):

1. Line 3 status as above.
2. `## Decision 1`: replace "The exact version is pinned at implementation time (Task 06.0 subtask 01)" with the pinned FTXUI version/commit and the verified facts (C++ standard floor, MSVC support, vcpkg port name, `Table` availability, Windows console behavior) taken from the 06.0 subtask 01 PR; delete sentences that were unverified claims (e.g. "most widely adopted") or replace them with the measured numbers (stars/releases date).
3. For each diagram D1-D10 whose golden (Task 07.0 subtasks 24-25) deviates in content or hotkeys: update the ADR diagram **or** record the deviation under a new `### As built` heading (the 06.0 rule: "deviations amend the ADR").
4. `## Launching`: sync the flag list with `url_shortener tui --help` (output pasted into the PR for verification).

**Decision log:** Q1, Q17, Q18 rows get a trailing `Resolved <date>: <outcome>` text; the "Things to research tomorrow" list items 1, 2 and 4 are struck or answered.

## Implementation steps

1. Confirm the user's acceptance instruction exists in the conversation or PR; otherwise stop after preparing the diff (do not flip statuses autonomously).
2. Make the edits above; run `/doc-xref docs/adr/0002-unified-command-model.md` and `/doc-xref docs/adr/0003-tui-framework-and-architecture.md` to find references that say "Proposed".
3. Fix those references (typically `docs/commands/README.md` line 3, `docs/roadmap/*/status.md`).

## Commands

```bash
git grep -n "Proposed" -- docs/adr docs/commands docs/roadmap/0006-unified-command-model-and-tui
cmake-build/url_shortener tui --help     # Windows: cmake-build\Debug\url_shortener.exe tui --help
```

## Tests

None executable. Validated by `/link-check docs/adr` and by `tests/unit/docs/*` staying green.

## Constraints

- Do not rewrite history sections (Context/Alternatives); only status, as-built and verified facts.
- Dates are real dates (ISO), not placeholders.

## Success criteria

1. Both ADR headers show the new status and acceptance date.
2. No document in `docs/` still calls these ADRs "Proposed" (`git grep` returns nothing outside decision-log history).
3. ADR 0003 states the pinned FTXUI version and contains no unverified adoption claim.

## Out of scope / risks

- Accepting ADRs is not an agent decision. Risk: if acceptance is withheld, 08.0 subtasks 05-07 must still say "Accepted design (ADR 0002)" vs "Proposed" consistently - use the real status wording everywhere.
