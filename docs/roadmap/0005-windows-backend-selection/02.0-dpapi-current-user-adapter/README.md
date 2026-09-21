# Task 02.0 - DPAPI CurrentUser adapter

**Parent milestone:** [Milestone 0005 - Windows Backend Selection](/docs/roadmap/0005-windows-backend-selection/plan.md)
**Status:** ⬜ Not started

## Scope

Implement the `dpapi-user` backend: a secret-store adapter that protects and
decrypts secret material using Windows DPAPI at CurrentUser scope
(`CryptProtectData`/`CryptUnprotectData` with no explicit entropy beyond what
the interface requires, and no `CRYPTPROTECT_LOCAL_MACHINE` flag). This is
the backend `dpapi-user` selects and the backend auto-selection (Task 05.0)
defaults to for interactive use.

This task implements the adapter class only. Wiring it into
`BuildWindowsSecretStore` and into auto-selection default behavior is Task
05.0; adapter-selection tests live in Task 06.0.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [DPAPI CurrentUser protect and unprotect](01-dpapi-current-user-protect-and-unprotect.md) | ⬜ Not started | 02 |
| 02 | [DPAPI CurrentUser ISecretStore conformance](02-dpapi-current-user-isecretstore-conformance.md) | ⬜ Not started | none |

## Key constraints

- Decrypt only under the same Windows user profile that encrypted the data -
  CurrentUser scope, never LocalMachine (plan.md Backend semantics).
- Never log or print decrypted plaintext, DPAPI blobs, or entropy values
  (plan.md C3).
- No cross-platform dependency introduced for this adapter (plan.md C6).
