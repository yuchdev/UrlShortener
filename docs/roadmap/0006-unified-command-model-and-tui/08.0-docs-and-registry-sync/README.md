# Task 08.0 - Docs and registry sync

**Parent milestone:** [Milestone 0006 - Unified Command Model and TUI](/docs/roadmap/0006-unified-command-model-and-tui/plan.md)
**Status:** ⬜ Not started
**Depends on:** [07.0](/docs/roadmap/0006-unified-command-model-and-tui/07.0-tests-and-conformance/README.md) (subtask 34 gates passed). Documentation drafts that only describe settled contracts (subtasks 01-04, 14-15) may start earlier; subtasks 05-13 describe shipped behavior and must be verified against the built binary.

## Scope

Make the documentation set coherent and true to the shipped system: accept
ADRs 0002/0003, finalize the generated command catalog, update the top-level
guides (`ARCHITECTURE.md`, `CLAUDE.md`, `README.md`), the reference docs
(`docs/cli`, `docs/api`, new `docs/tui`), the test docs, the docs registry,
and reconcile Milestones 0002 and 0004 with the command model. Ends with
`/link-check`, `/doc-registry` and a claims-versus-binary spot check.

**Ownership boundary (to avoid double authoring):**

| Document | Authored by | 08.0 does |
|---|---|---|
| `docs/commands/adding-a-transport.md` | Task 05.0 (subtask on `ITransportAdapter`/`serve-stdio`) | verify, cross-link, registry row (subtask 04) |
| `docs/cli/README.md` storage-limitation paragraph and ADR 0001 Negative/Non-goals text | Task 02.0 (subtask on persistence tests and ADR update) | verify they were updated; do not rewrite (subtask 08) |
| `--help` text, `command.list` output, `docs/commands/README.md` sections 3 and 5 | generated (Task 01.0 catalog generator) | never hand-edit generated regions (subtask 03) |
| `docs/api/README.md` | generated from `RouteRegistry` | regenerate and verify only (subtask 09) |
| `docs/testing/tui_manual_smoke.md` | Task 06.0 (manual smoke checklist subtask) | link from index, do not rewrite (subtask 12) |
| `docs/testing/testplans/tui.md` | Task 07.0 subtask 29 | link from index, do not rewrite (subtask 12) |
| everything else listed in the subtasks below | 08.0 | author/update |

## Mini-graph

```text
01 -> 02            ADR statuses (gate: human acceptance) 
03 -> 04            catalog final, transport guide verification
05, 06, 07          top-level guides (parallel, after 03)
08, 09, 10 -> 11    CLI/API/TUI reference (parallel), TUI dev guide after the guide
12 -> 13            testing docs, then registry/agent docs
14, 15              0002 / 0004 reconciliation (independent; may run early)
16                  roadmap index + milestone status (after all)
17                  final gates
```

## Subtasks

| # | Document | Status | Depends on | Blocks |
|---|----------|--------|------------|--------|
| 01 | [ADR 0002/0003 acceptance gate](01-adr-0002-0003-acceptance-gate.md) | ⬜ Not started | 07.0/34 | 02, 16 |
| 02 | [ADR 0001 amendment and ADR index](02-adr-0001-amendment-and-adr-index.md) | ⬜ Not started | 01 | 16 |
| 03 | [Command catalog finalization](03-command-catalog-finalization.md) | ⬜ Not started | 01.0, 07.0/16 | 04, 05-07 |
| 04 | [Adding-a-transport guide verification](04-adding-a-transport-verification.md) | ⬜ Not started | 03, 05.0 | 13 |
| 05 | [ARCHITECTURE.md update](05-architecture-md-update.md) | ⬜ Not started | 03 | 17 |
| 06 | [CLAUDE.md update](06-claude-md-update.md) | ⬜ Not started | 03 | 17 |
| 07 | [README.md update](07-readme-md-update.md) | ⬜ Not started | 03 | 17 |
| 08 | [CLI reference update](08-cli-reference-update.md) | ⬜ Not started | 02.0, 04.0, 05.0, 06.0 | 17 |
| 09 | [API reference regeneration](09-api-reference-regeneration.md) | ⬜ Not started | 04.0, 07.0/17 | 17 |
| 10 | [TUI user guide](10-tui-user-guide.md) | ⬜ Not started | 06.0, 07.0/24, 07.0/25 | 11, 13, 17 |
| 11 | [TUI development guide](11-tui-development-guide.md) | ⬜ Not started | 10, 07.0/22-28 | 13, 17 |
| 12 | [Testing docs sync](12-testing-docs-sync.md) | ⬜ Not started | 07.0 | 13, 17 |
| 13 | [Docs registry and agent docs](13-docs-registry-and-agent-docs.md) | ⬜ Not started | 04, 10, 11, 12 | 17 |
| 14 | [Reconcile Milestone 0002](14-reconcile-milestone-0002.md) | ⬜ Not started | plan.md | 16 |
| 15 | [Reconcile Milestone 0004](15-reconcile-milestone-0004.md) | ⬜ Not started | plan.md | 16 |
| 16 | [Roadmap index and milestone status sync](16-roadmap-index-and-status-sync.md) | ⬜ Not started | 01, 02, 14, 15 | 17 |
| 17 | [Final link, registry and claims gates](17-final-link-registry-and-claims-gates.md) | ⬜ Not started | 05-13, 16 | none |

## Key constraints

- Links use the absolute-from-repo-root convention of [docs/roadmap/README.md](/docs/roadmap/README.md) inside `docs/roadmap/**`; other docs follow the local convention of the file being edited (relative links in `docs/cli`, `docs/testing`, `docs/README.md`; repo-relative in `README.md`, `ARCHITECTURE.md`, `CLAUDE.md`).
- Every factual claim about flags, exit codes, hotkeys or file paths must be checked against the built binary or the source (subtask 17 spot check); unverified statements are marked or removed.
- No secrets, DSNs or real credentials in examples (`secret-scan` hook).
- Docs-only: no changes under `src/`, `include/`, `tests/`, CMake; any defect found goes back to its owning task.
- ADR acceptance is a human decision (decision-log Q17); agents prepare the edit but must not flip a status without explicit user instruction.

## Exit criteria

1. ADR 0002 and ADR 0003 headers read `Accepted` (or `Implemented`), ADR 0001 reads `Accepted (amended by 0002)`, and `docs/adr/README.md` matches.
2. `docs/commands/README.md` is generated (drift test green) and states so; `adding-a-transport.md` is linked from the catalog, ADR 0002 and `docs/README.md`.
3. `ARCHITECTURE.md`, `CLAUDE.md`, `README.md`, `docs/cli`, `docs/api`, `docs/tui`, `docs/testing` describe the shipped system; the dangling ADR links in `ARCHITECTURE.md` are gone.
4. 0002 and 0004 plan/task docs carry the reconciliation edits; `docs/roadmap/README.md` and 0006 `status.md` show the final state.
5. `/link-check` and `/doc-registry` report zero findings for `docs/`, repo-root Markdown and `.claude/`.
