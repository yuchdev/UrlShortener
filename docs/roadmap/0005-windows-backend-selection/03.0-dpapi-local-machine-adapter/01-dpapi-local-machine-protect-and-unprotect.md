# 01 - DPAPI LocalMachine protect and unprotect

**Parent task:** 03.0 DPAPI LocalMachine adapter
**State:** ⬜ Not started
**Depends on:** none
**Blocks:** none

## Objective

Implement the core encrypt/decrypt operations for the `dpapi-machine`
backend using `CryptProtectData`/`CryptUnprotectData` with the
`CRYPTPROTECT_LOCAL_MACHINE` flag, and implement `ISecretStore` conformance
in the same subtask (unlike Task 02.0, this adapter's surface is small
enough that protect/unprotect and interface conformance are one unit of
work; see Subtask 02 for the ACL/scope-constraint follow-on).

## Files

- `include/url_shortener/secret_store/windows/dpapi_local_machine_secret_store.hpp`
- `src/secret_store/windows/dpapi_local_machine_secret_store.cpp`

## Behavior (plan.md Backend semantics: `dpapi-machine`)

- Protect with `CryptProtectData` using the `CRYPTPROTECT_LOCAL_MACHINE`
  flag.
- Use for services that need secrets readable by more than one service
  account on the same host.
- Implement `ISecretStore` (same interface as
  `DpapiCurrentUserSecretStore` from Task 02.0), preserving existing
  create/read/update/delete command behavior.

## Shape

```cpp
class DpapiLocalMachineSecretStore final : public ISecretStore {
public:
    std::vector<std::byte> Protect(std::span<const std::byte> plaintext) const;
    std::vector<std::byte> Unprotect(std::span<const std::byte> ciphertext) const;

    // ISecretStore overrides delegate through Protect/Unprotect plus
    // whatever persistence ISecretStore itself contracts for.
};
```

## Constraints

- `CRYPTPROTECT_LOCAL_MACHINE` must be set on every `CryptProtectData` call
  in this file - omitting it would silently downgrade to CurrentUser scope,
  which plan.md C2 forbids.
- On DPAPI failure, surface `GetLastError()` without secret bytes (plan.md
  C3), same pattern as Task 02.0 Subtask 01.
- Do not change `ISecretStore` itself.

## Success criteria

- [ ] `Protect`/`Unprotect` round-trip using LocalMachine scope
      (`CRYPTPROTECT_LOCAL_MACHINE` verified present on the protect call).
- [ ] Ciphertext produced by this adapter is decryptable by a different user
      account on the same machine (LocalMachine scope), unlike Task 02.0's
      CurrentUser adapter - verified in Task 06.0's integration tests.
- [ ] `DpapiLocalMachineSecretStore` satisfies `ISecretStore` and compiles
      behind `std::unique_ptr<ISecretStore>`.
- [ ] A DPAPI failure produces an exception with the Windows error code and
      no secret bytes.
