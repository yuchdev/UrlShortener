# 01 - Parser and validation unit tests

**Parent task:** 06.0 Test suite
**State:** ⬜ Not started
**Depends on:** none (exercises Task 01.0's production code)
**Blocks:** none

## Objective

Cover `ParseWindowsSecretBackend` and `ValidateWindowsSecretBackendPlatform`
(Task 01.0) with unit tests, including the two concrete test cases from
plan.md's "Tests" section that are about value/platform validation:

- "Invalid backend values fail with the valid Windows values in the
  message."
- "Linux-only backend names are rejected on Windows."

## Files

- `tests/unit/secret_store/windows/01_parse_valid_backend_values.cpp`
- `tests/unit/secret_store/windows/02_invalid_backend_value_rejected.cpp`
- `tests/unit/secret_store/windows/03_linux_backend_name_rejected_on_windows.cpp`
- `tests/unit/secret_store/windows/04_windows_backend_rejected_on_non_windows_platform.cpp`

Register each new test binary in `sources.cmake` and `CMakeLists.txt`,
following the pattern used for `tests/unit/config/*.cpp`.

## Test cases

`01_parse_valid_backend_values.cpp`:
- `ParseWindowsSecretBackend("dpapi-user")` returns `dpapi_user`.
- `ParseWindowsSecretBackend("dpapi-machine")` returns `dpapi_machine`.
- `ParseWindowsSecretBackend("wincred")` returns `wincred`.
- `ToCliValue` is the exact inverse for all three.

`02_invalid_backend_value_rejected.cpp`:
- An unrecognized string (e.g. `"bogus"`) throws `std::runtime_error`.
- The exception message contains all three valid Windows values
  (`dpapi-user`, `dpapi-machine`, `wincred`).
- An empty string is rejected the same way.

`03_linux_backend_name_rejected_on_windows.cpp`:
- Each Linux backend name accepted by the Linux CLI is rejected by
  `ValidateWindowsSecretBackendPlatform` when the platform is Windows.
- The exception message names the three valid Windows values, not just
  "invalid value."

`04_windows_backend_rejected_on_non_windows_platform.cpp`:
- Each of `dpapi-user`, `dpapi-machine`, `wincred` is rejected by
  `ValidateWindowsSecretBackendPlatform` when compiled/run for a non-Windows
  platform, with a message stating the backend is Windows-only.
- This test is compiled only where the non-Windows code path exists (guard
  per the platform-detection convention Task 01.0 Subtask 03 establishes).

## Constraints

- Unit tests must not require an actual Windows machine to exercise the
  parsing/validation logic itself - only the DPAPI/Credential Manager calls
  are Windows-only; parsing is pure string logic and must be testable on any
  build platform this project's CI runs.

## Success criteria

- [ ] All four test files exist, are registered in `sources.cmake`/
      `CMakeLists.txt`, and pass via `ctest -L unit`.
- [ ] Every plan.md "Tests" bullet about invalid values and Linux-name
      rejection has a corresponding assertion.
