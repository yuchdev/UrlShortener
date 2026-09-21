# 02 - Factory adapter selection unit tests

**Parent task:** 06.0 Test suite
**State:** ⬜ Not started
**Depends on:** none (exercises Tasks 02.0-05.0's production code)
**Blocks:** none

## Objective

Cover `BuildWindowsSecretStore` (Task 05.0) with unit tests proving each
`--backend` value reaches the correct adapter and no other, plus the
omitted-backend auto-selection path. These are the concrete test cases from
plan.md's "Tests" section:

- "`--backend dpapi-user` selects the DPAPI CurrentUser adapter."
- "`--backend dpapi-machine` selects the DPAPI LocalMachine adapter."
- "`--backend wincred` selects the Credential Manager adapter."
- "Omitted backend preserves current auto-selection behavior."

And, from plan.md's Backend semantics section:

- "Tests should verify that backend selection reaches the CurrentUser
  adapter and does not call machine-scope or Credential Manager code."
  (`dpapi-user`)
- "Tests should verify the LocalMachine adapter is selected only when
  requested." (`dpapi-machine`)

## Files

- `tests/unit/secret_store/windows/05_factory_selects_dpapi_user_adapter.cpp`
- `tests/unit/secret_store/windows/06_factory_selects_dpapi_machine_adapter.cpp`
- `tests/unit/secret_store/windows/07_factory_selects_wincred_adapter.cpp`
- `tests/unit/secret_store/windows/08_omitted_backend_preserves_auto_selection.cpp`

Register each in `sources.cmake`/`CMakeLists.txt`.

## Test cases

`05_factory_selects_dpapi_user_adapter.cpp`:
- `BuildWindowsSecretStore(WindowsSecretBackend::dpapi_user)` returns an
  instance that is a `DpapiCurrentUserSecretStore` (e.g. via `dynamic_cast`
  or a type-tag accessor), and that no LocalMachine or Credential Manager
  API surface is touched by constructing it (use a fake/mockable Win32
  Credential Manager surface, per Task 04.0 Subtask 01's injectable design,
  to assert zero calls when the selected backend is `dpapi-user`).

`06_factory_selects_dpapi_machine_adapter.cpp`:
- `BuildWindowsSecretStore(WindowsSecretBackend::dpapi_machine)` returns a
  `DpapiLocalMachineSecretStore`, and is selected *only* when
  `dpapi_machine` is explicitly requested - i.e. `dpapi-user` and
  `wincred` requests never construct this adapter.

`07_factory_selects_wincred_adapter.cpp`:
- `BuildWindowsSecretStore(WindowsSecretBackend::wincred)` returns a
  `WincredSecretStore`, using the fake Credential Manager adapter (not real
  `Cred*` calls).

`08_omitted_backend_preserves_auto_selection.cpp`:
- Running the CLI wiring (Task 05.0 Subtask 02) with no `--backend` resolves
  to the same adapter type as `--backend dpapi-user` would.

## Constraints

- These tests must use fake adapters/injectable Win32 surfaces - they must
  not perform real DPAPI protect calls or real Credential Manager writes
  (that is Subtask 04's job, opt-in only).

## Success criteria

- [ ] All four test files exist, are registered, and pass via
      `ctest -L unit`.
- [ ] Each of the five plan.md "Tests"/"Backend semantics" bullets listed
      above has a corresponding assertion, exactly once across this file
      set.
