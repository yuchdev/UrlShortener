# 03 - Secret-safety assertions

**Parent task:** 06.0 Test suite
**State:** ⬜ Not started
**Depends on:** none (exercises Tasks 01.0-05.0's error paths)
**Blocks:** none

## Objective

Assert, across every failure path introduced by this milestone, that no test
output contains secret values - plan.md's "Tests" bullet: "No test output
contains secret values," and plan.md C3.

## Files

- `tests/unit/secret_store/windows/09_no_secret_material_in_diagnostic_output.cpp`

Register in `sources.cmake`/`CMakeLists.txt`.

## Test cases

- Feed a known "secret-shaped" byte sequence (e.g. a recognizable marker
  string) through:
  - `DpapiCurrentUserSecretStore`/`DpapiLocalMachineSecretStore` failure
    paths (simulate a DPAPI failure via a fault-injectable seam, if one
    exists from Tasks 02.0/03.0, or via an intentionally invalid ciphertext
    input that triggers `CryptUnprotectData` failure).
  - `WincredSecretStore` failure paths (fake Credential Manager adapter
    returns a failure code for a write/read containing the marker).
  - `ParseWindowsSecretBackend`/`ValidateWindowsSecretBackendPlatform`
    failure paths (Task 01.0) - confirm these never had secret material to
    begin with, since they only ever see the literal `--backend` string.
  - The omitted-backend auto-selection failure path (Task 05.0 Subtask 02).
- Assert the marker byte sequence never appears in any exception `what()`
  string, any diagnostic output the command prints, or any captured log
  line produced during the test.

## Constraints

- This test must exercise the actual exception messages/diagnostic strings
  produced by production code, not re-implement its own "would this leak"
  logic - i.e. call the real `Protect`/`Unprotect`/`Create`/`Read`/`Update`/
  `Delete` methods and inspect what they actually throw/print.

## Success criteria

- [ ] The marker-based assertion passes against every failure path listed
      above.
- [ ] Adding a call that accidentally interpolates secret bytes into an
      error message anywhere in this milestone's code would make this test
      fail.
