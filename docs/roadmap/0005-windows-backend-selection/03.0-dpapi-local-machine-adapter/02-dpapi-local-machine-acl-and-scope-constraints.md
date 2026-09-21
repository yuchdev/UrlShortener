# 02 - DPAPI LocalMachine ACL and scope constraints

**Parent task:** 03.0 DPAPI LocalMachine adapter
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** none

## Objective

Ensure the LocalMachine adapter's ciphertext-persistence path does not
undermine the filesystem-ACL protection that LocalMachine-scope secrets rely
on, since DPAPI LocalMachine scope on its own only protects data at rest
against *other machines* - any local account can decrypt it. The mitigating
control is restrictive filesystem ACLs on wherever the ciphertext is
persisted.

This subtask is implementation-side enforcement; the operator-facing
explanation of the tradeoff (why LocalMachine is broader trust than
CurrentUser, and that it must be paired with ACLs) is written in Task 07.0.

## Files

- `src/secret_store/windows/dpapi_local_machine_secret_store.cpp` (file
  creation/permission handling for any ciphertext this adapter writes)

## Behavior

- When this adapter creates or updates a persisted ciphertext file/entry, do
  not widen its ACLs beyond what the existing persistence path already
  applies - if the existing storage mechanism (shared with other backends)
  already restricts access appropriately, this adapter must not bypass or
  loosen that.
- Do not introduce a separate, more permissive persistence path "for
  convenience" - LocalMachine-protected ciphertext goes through the same
  storage mechanism as the other Windows adapters use.

## Constraints

- No new cross-platform or Windows-specific ACL dependency beyond what the
  existing persistence layer already uses (plan.md C6, "no cross-platform
  dependencies").
- This subtask does not define the ACL policy itself (that is an operations/
  deployment concern documented in Task 07.0) - it only guards against the
  adapter regressing existing protections.

## Success criteria

- [ ] Ciphertext written by `DpapiLocalMachineSecretStore` inherits the same
      file/entry permissions as ciphertext written by the other Windows
      adapters (no adapter-specific ACL widening).
- [ ] Code review / test coverage (Task 06.0) confirms no permission flags
      are passed that would make LocalMachine ciphertext world-readable or
      broader than the existing persistence default.
