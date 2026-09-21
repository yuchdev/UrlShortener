# 01 - BuildWindowsSecretStore factory

**Parent task:** 05.0 Factory wiring & auto-selection
**State:** ⬜ Not started
**Depends on:** none (depends on Tasks 01.0-04.0's outputs, all outside this
milestone's own subtask graph)
**Blocks:** 02

## Objective

Implement the factory function that constructs the correct adapter for a
resolved `WindowsSecretBackend`.

## Files

- `include/url_shortener/secret_store/windows/windows_secret_store_factory.hpp`
- `src/secret_store/windows/windows_secret_store_factory.cpp`

## Shape

```cpp
/**
 * @brief Construct the Windows secret-store adapter matching `backend`.
 *
 * @param backend Already-validated backend selection (see
 *        ParseWindowsSecretBackend / ValidateWindowsSecretBackendPlatform,
 *        Task 01.0).
 * @return A ready-to-use adapter implementing ISecretStore.
 */
std::unique_ptr<ISecretStore> BuildWindowsSecretStore(WindowsSecretBackend backend);
```

Implementation is a straightforward switch over the three enumerators,
constructing `DpapiCurrentUserSecretStore`, `DpapiLocalMachineSecretStore`,
or `WincredSecretStore` respectively (Tasks 02.0-04.0). No default case that
silently substitutes a backend - an unhandled enumerator is a programming
error (`assert`/`std::unreachable`-style), not a runtime fallback, since
`WindowsSecretBackend` can only ever hold one of the three values by
construction (Task 01.0).

## Constraints

- Take backend construction as a pure function of the `WindowsSecretBackend`
  value - no hidden environment/config lookups inside the factory itself.
- Keep CLI parsing separate from backend construction (plan.md Architecture)
  - this factory must not re-parse or re-validate `--backend` string values;
  it only accepts the already-typed enum.

## Success criteria

- [ ] `BuildWindowsSecretStore(WindowsSecretBackend::dpapi_user)` returns a
      `DpapiCurrentUserSecretStore` behind `ISecretStore`.
- [ ] `BuildWindowsSecretStore(WindowsSecretBackend::dpapi_machine)` returns
      a `DpapiLocalMachineSecretStore` behind `ISecretStore`.
- [ ] `BuildWindowsSecretStore(WindowsSecretBackend::wincred)` returns a
      `WincredSecretStore` behind `ISecretStore`.
- [ ] No code path in this function returns a different backend than the one
      requested.
