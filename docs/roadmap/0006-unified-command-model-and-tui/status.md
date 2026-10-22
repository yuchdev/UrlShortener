# Milestone 0006 - Unified Command Model and TUI - Status

Tracks progress against [plan.md](/docs/roadmap/0006-unified-command-model-and-tui/plan.md).

## Current status

| Task | Name | Status | Tests |
|------|------|--------|-------|
| 01.0 | Command model core | ⬜ Not started | 0 |
| 02.0 | Storage path unification | ⬜ Not started | 0 |
| 03.0 | Link and browse commands | ⬜ Not started | 0 |
| 04.0 | REST and CLI adapters | ⬜ Not started | 0 |
| 05.0 | Jobs, pair import/export, stdio | ⬜ Not started | 0 |
| 06.0 | Terminal UI | ⬜ Not started | 0 |
| 07.0 | Tests and conformance | ⬜ Not started | 0 |
| 08.0 | Docs and registry sync | ⬜ Not started | 0 |

**Legend:** ✅ Complete · 🔶 In progress / partial · ⬜ Not started

**Current gate status:** design complete, decisions confirmed, implementation
not started (2026-10-22). Delivered so far:
[ADR 0002](/docs/adr/0002-unified-command-model.md) and
[ADR 0003](/docs/adr/0003-tui-framework-and-architecture.md) (both still
**Proposed** - intentionally; see below), the [command catalog](/docs/commands/README.md),
this milestone's plan and task/subtask specs, and (2026-10-22) a human review
of all D1-D16 open decisions plus a new Settings screen (D13 in ADR 0003,
Task 06.0 subtask 21) added after the headless design pass. No code has
changed; no build or test was run. Branch `roadmap/tui-unified-command-model`,
merged to `master` locally (not yet pushed).

**D1-D16 are confirmed, not open.** Every recommendation in
[decision-log.md](/docs/roadmap/0006-unified-command-model-and-tui/decision-log.md)
was reviewed with the user on 2026-10-22 and adopted as-is (see that file's
"Human review" section) - none were reversed. This un-blocks every owning
subtask listed in the decision log.

**ADR 0002/0003 stay `Proposed` on purpose**, even after the D1-D16 review:
flipping them to `Accepted` is Task 08.0 subtask 01's job, which requires Task
07.0's gates to have actually passed and real artifacts (pinned FTXUI
version, `tui --help` output, as-built deltas) that do not exist before any
code is written. Confirming the decisions removes the planning-level
blocker; it is not a substitute for that gate.

## Open risks

- **Storage unification (02.0)** touches the path every link command and the
  redirect fast path depend on; it is the highest-risk task and is gated by
  the existing contract suite plus the protected-fast-path rule (C10: behavior
  and complexity unchanged; redirect counters are persisted only by a
  background flusher, per confirmed decision D12).
- **No JSON library**: one bounded top-level scanner (Task 01.0 subtask 01) is
  the only sanctioned deviation from the manual helpers (per confirmed
  decision D3); escalate to a separate ADR rather than adding a dependency
  inline.
- **New HTTP surface on an unauthenticated server**: `/api/v1/commands`,
  `GET /api/v1/links`, `/api/v1/jobs*` are off by default and role-limited
  until Milestone 0002 (per confirmed decision D4).
- **FTXUI claims unverified** (version, MSVC build, Windows console behavior)
  - see [decision-log.md](/docs/roadmap/0006-unified-command-model-and-tui/decision-log.md);
  still scoped to Task 06.0 subtask 01, unchanged by the 2026-10-22 review.
- **Overlap with 0002** pagination/error contracts; reconciliation is a
  documented projection but must be re-checked when 0002 is scheduled.
- **Tauri** has no spec; its relationship to 0002 is an open question (Q14),
  explicitly left open on 2026-10-22 - nothing in 0006 depends on the answer.

## Notes & decisions

See [decision-log.md](/docs/roadmap/0006-unified-command-model-and-tui/decision-log.md)
for every choice made without asking the user, with alternatives.
