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

**Current gate status:** design only (2026-10-19). Delivered so far:
[ADR 0002](/docs/adr/0002-unified-command-model.md) and
[ADR 0003](/docs/adr/0003-tui-framework-and-architecture.md) (both
**Proposed**), the [command catalog](/docs/commands/README.md), this
milestone's plan and task/subtask specs. No code has changed; no build or
test was run. Branch `roadmap/tui-unified-command-model`.

## Open risks

- **Storage unification (02.0)** touches the path every link command and the
  redirect fast path depend on; it is the highest-risk task and is gated by
  the existing contract suite plus the protected-fast-path rule (C10: behavior
  and complexity unchanged; redirect counters are persisted only by a
  background flusher, open decision D12).
- **No JSON library**: one bounded top-level scanner (Task 01.0 subtask 01) is
  the only sanctioned deviation from the manual helpers (open decision D3);
  escalate to a separate ADR rather than adding a dependency inline.
- **New HTTP surface on an unauthenticated server**: `/api/v1/commands`,
  `GET /api/v1/links`, `/api/v1/jobs*` are off by default and role-limited
  until Milestone 0002 (open decision D4).
- **Open decisions D1-D12** in the decision log need a human answer before the
  owning subtasks start.
- **FTXUI claims unverified** (version, MSVC build, Windows console behavior)
  - see [decision-log.md](/docs/roadmap/0006-unified-command-model-and-tui/decision-log.md).
- **Overlap with 0002** pagination/error contracts; reconciliation is a
  documented projection but must be re-checked when 0002 is scheduled.
- **Tauri** has no spec; its relationship to 0002 is an open question (Q14).

## Notes & decisions

See [decision-log.md](/docs/roadmap/0006-unified-command-model-and-tui/decision-log.md)
for every choice made without asking the user, with alternatives.
