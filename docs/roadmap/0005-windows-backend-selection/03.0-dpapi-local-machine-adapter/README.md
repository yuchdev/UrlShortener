# Task 03.0 - DPAPI LocalMachine adapter

**Parent milestone:** [Milestone 0005 - Windows Backend Selection](/docs/roadmap/0005-windows-backend-selection/plan.md)
**Status:** ⬜ Not started

## Scope

Implement the `dpapi-machine` backend: a secret-store adapter that protects
and decrypts secret material using Windows DPAPI at LocalMachine scope
(`CryptProtectData`/`CryptUnprotectData` with `CRYPTPROTECT_LOCAL_MACHINE`).
This backend is for services that need secrets readable by more than one
service account on the same host, and is deliberately broader-trust than
`dpapi-user` (Task 02.0).

This task implements the adapter class only. Wiring it into
`BuildWindowsSecretStore` is Task 05.0; adapter-selection tests live in Task
06.0; the operator-facing ACL guidance is written up in Task 07.0.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [DPAPI LocalMachine protect and unprotect](01-dpapi-local-machine-protect-and-unprotect.md) | ⬜ Not started | none |
| 02 | [DPAPI LocalMachine ACL and scope constraints](02-dpapi-local-machine-acl-and-scope-constraints.md) | ⬜ Not started | none |

## Key constraints

- LocalMachine scope is broader than CurrentUser and must be paired with
  filesystem ACLs on any persisted ciphertext (plan.md Backend semantics -
  the concrete operator-facing tradeoff writeup is Task 07.0, but the
  adapter itself must not undermine ACL protection, e.g. by writing
  ciphertext with permissive permissions).
- Never log or print decrypted plaintext or DPAPI blobs (plan.md C3).
- No cross-platform dependency introduced for this adapter (plan.md C6).
