# 04 - Opt-in Windows integration tests

**Parent task:** 06.0 Test suite
**State:** ⬜ Not started
**Depends on:** none (exercises Tasks 02.0-04.0's real Windows API calls)
**Blocks:** none

## Objective

Add opt-in, Windows-only integration tests that exercise the real DPAPI and
Credential Manager APIs, per plan.md's "Tests" section: "Integration tests
that touch real DPAPI or Credential Manager APIs must be opt-in and clearly
marked as Windows-only."

## Files

- `tests/integration/secret_store/windows/01_dpapi_current_user_roundtrip.cpp`
- `tests/integration/secret_store/windows/02_dpapi_local_machine_roundtrip.cpp`
- `tests/integration/secret_store/windows/03_wincred_roundtrip.cpp`

Register each in `sources.cmake`/`CMakeLists.txt` under the project's
existing integration-test target/label convention (`ctest -L integration`),
and gate them so they do not run as part of the default `unit` label - follow
whatever opt-in mechanism (CMake option, CTest label, environment variable)
the existing `tests/integration/` suite already uses.

## Test cases

`01_dpapi_current_user_roundtrip.cpp`:
- Real `CryptProtectData`/`CryptUnprotectData` round-trip at CurrentUser
  scope on the executing account.
- Clean up any ciphertext artifact the test leaves behind.

`02_dpapi_local_machine_roundtrip.cpp`:
- Real `CryptProtectData`/`CryptUnprotectData` round-trip at LocalMachine
  scope.
- Clean up any ciphertext artifact the test leaves behind.

`03_wincred_roundtrip.cpp`:
- Real `CredWrite`/`CredRead`/`CredDelete` round-trip using a target name
  clearly marked as a test artifact (e.g. a namespace segment identifying
  it as a test credential), so it is identifiable and removable if cleanup
  is interrupted.
- Test must `CredDelete` the entry it creates, including on assertion
  failure (RAII cleanup guard), to avoid leaving real Credential Manager
  entries behind - this is the concrete enforcement of plan.md's "do not
  write real user credentials during unit tests" rule for the one place
  this milestone deliberately does write one (opt-in integration only, never
  the default unit run).

## Constraints

- Must be clearly marked Windows-only (compiled/registered only on Windows
  builds).
- Must be opt-in - not part of the default test run invoked by
  `ctest -L unit` or by the project's Stop-hook/CI gate unless that gate
  explicitly opts into the integration label on a Windows runner.
- Must not depend on any state left over from a previous run (idempotent
  target names, or a setup/teardown pair per test case).

## Success criteria

- [ ] All three test files exist, are registered, and are excluded from the
      default `unit` label.
- [ ] Running them explicitly (`ctest -L integration` or equivalent) on a
      Windows host with the appropriate opt-in flag passes and leaves no
      residual DPAPI ciphertext files or Credential Manager entries behind.
