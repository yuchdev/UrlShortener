# Task 04.0 - Windows Credential Manager adapter

**Parent milestone:** [Milestone 0005 - Windows Backend Selection](/docs/roadmap/0005-windows-backend-selection/plan.md)
**Status:** ⬜ Not started

## Scope

Implement the `wincred` backend: a secret-store adapter over the Windows
Credential Manager APIs (`CredWrite`, `CredRead`, `CredDelete`, `CredEnumerate`
as needed). Entries are user-visible through Windows' own Credential Manager
UI, so target-name stability and namespacing matter more here than for the
DPAPI adapters.

This task implements the adapter class only. Wiring it into
`BuildWindowsSecretStore` is Task 05.0; adapter-selection tests and the
"use fake Credential Manager adapter, never write real credentials in unit
tests" rule live in Task 06.0; the target-naming rules are documented for
operators in Task 07.0.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [wincred target naming and CRUD preservation](01-wincred-target-naming-and-crud-preservation.md) | ⬜ Not started | 02 |
| 02 | [wincred ISecretStore conformance](02-wincred-isecretstore-conformance.md) | ⬜ Not started | none |

## Key constraints

- Use stable target names that include application namespace and secret key
  (plan.md Backend semantics).
- Preserve existing create/read/update/delete command behavior.
- Never log or print credential blobs (plan.md C3).
- Unit tests must use a fake Credential Manager adapter - this task's
  production code must be structured so that substitution is possible (an
  injectable Credential Manager API surface), even though the fake itself is
  built in Task 06.0.
