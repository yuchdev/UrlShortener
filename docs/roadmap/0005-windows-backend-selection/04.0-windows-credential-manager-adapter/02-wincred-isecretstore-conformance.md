# 02 - wincred ISecretStore conformance

**Parent task:** 04.0 Windows Credential Manager adapter
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** none

## Objective

Make `WincredSecretStore` implement `ISecretStore`, matching the conformance
work done for the DPAPI adapters in Tasks 02.0 and 03.0, so
`BuildWindowsSecretStore` (Task 05.0) can return it uniformly.

## Files

- `include/url_shortener/secret_store/windows/wincred_secret_store.hpp` (add
  `: public ISecretStore`)
- `src/secret_store/windows/wincred_secret_store.cpp`

## Behavior

- Implement every `ISecretStore` method in terms of the `Create`/`Read`/
  `Update`/`Delete` primitives from Subtask 01.
- Preserve existing create/read/update/delete command behavior/semantics
  through the `ISecretStore` boundary, same as the DPAPI adapters.

## Constraints

- Do not change `ISecretStore` itself.
- No credential values in any exception thrown across the `ISecretStore`
  boundary (plan.md C3).

## Success criteria

- [ ] `WincredSecretStore` satisfies `ISecretStore` and compiles behind
      `std::unique_ptr<ISecretStore>`.
- [ ] Existing create/read/update/delete command behavior is unchanged when
      backed by this adapter (verified via Task 06.0 fake-adapter tests).
