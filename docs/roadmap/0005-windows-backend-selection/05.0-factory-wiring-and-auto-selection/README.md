# Task 05.0 - Factory wiring & auto-selection

**Parent milestone:** [Milestone 0005 - Windows Backend Selection](/docs/roadmap/0005-windows-backend-selection/plan.md)
**Status:** ⬜ Not started

## Scope

Implement `BuildWindowsSecretStore`, the factory that turns a validated
`WindowsSecretBackend` (Task 01.0) into the matching adapter
(`DpapiCurrentUserSecretStore` / `DpapiLocalMachineSecretStore` /
`WincredSecretStore` from Tasks 02.0-04.0), and wire the omitted-`--backend`
auto-selection path plus its non-secret diagnostic reporting requirement.

This is the task that connects Tasks 01.0-04.0 into a working selection
pipeline; Task 06.0's factory-selection tests exercise this code, and Task
07.0's migration-note documentation describes its externally observable
behavior.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [BuildWindowsSecretStore factory](01-build-windows-secret-store-factory.md) | ⬜ Not started | 02 |
| 02 | [Omitted-backend auto-selection and diagnostics](02-omitted-backend-auto-selection-and-diagnostics.md) | ⬜ Not started | none |

## Key constraints

- Construct the matching adapter and pass it to existing command handlers
  (plan.md Architecture, step 3-4) - no command handler should need to know
  which concrete adapter it received.
- A requested backend must never silently fall back to a different backend
  (plan.md C2).
- Never print secret material in diagnostics; backend API failures surface
  the Windows error code only (plan.md C3).
