# Task 07.0 - Documentation

**Parent milestone:** [Milestone 0005 - Windows Backend Selection](/docs/roadmap/0005-windows-backend-selection/plan.md)
**Status:** ⬜ Not started

## Scope

Update the operator/developer documentation with everything plan.md's
"Documentation updates" section calls for: a Windows backend table and
examples, the CurrentUser-vs-LocalMachine DPAPI security tradeoff, Credential
Manager naming rules, and a migration note for users currently relying on
automatic Windows backend selection.

This task is written last in the dependency graph because it documents
implemented, tested behavior (Tasks 01.0-06.0) rather than proposed
behavior - it should not be started until the behavior it describes is
implemented, to avoid documenting an API shape that changes during
implementation.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Windows backend table and DPAPI tradeoffs](01-windows-backend-table-and-dpapi-tradeoffs.md) | ⬜ Not started | none |
| 02 | [wincred naming and migration note](02-wincred-naming-and-migration-note.md) | ⬜ Not started | none |

## Key constraints

- This repository has no single top-level docs index to update (unlike
  `docs/backend-selection.md`'s storage-backend table, which is a standalone
  page linked from wherever storage backends are discussed) - link the new
  Windows secret-backend doc from `docs/backend-selection.md` and/or
  `README.md` if those are the natural entry points a reader would already
  be on, rather than inventing a new index file.
- Documentation must describe actually-implemented behavior; do not restate
  aspirational/planned behavior as if shipped.
