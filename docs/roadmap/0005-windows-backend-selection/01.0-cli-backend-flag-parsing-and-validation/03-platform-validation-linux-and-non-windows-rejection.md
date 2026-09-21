# 03 - Platform validation: Linux and non-Windows rejection

**Parent task:** 01.0 CLI backend flag parsing & validation
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** none

## Objective

Validate a parsed backend request against the current platform/build,
implementing plan.md C4 (Linux backend names rejected on Windows) and C5
(Windows backends are Windows-only).

This is "Validate the requested value against the current platform" (plan.md
Architecture, step 2), kept separate from `ParseWindowsSecretBackend` itself
(Subtask 01) so the value-recognition rules and the platform rules can be
tested independently.

## Files

- `include/url_shortener/secret_store/windows/windows_secret_backend.hpp`
  (extend with the platform-validation entry point declared alongside the
  enum from Subtask 01)
- `src/secret_store/windows/windows_secret_backend.cpp`

## Shape

```cpp
/**
 * @brief Validate that `value` is an acceptable backend selection for the
 *        platform this binary is running/built for.
 *
 * @throws std::runtime_error with a Windows-only explanation if `value` is a
 *         recognized Linux backend name (e.g. "pass", "gnome-keyring" - the
 *         Linux CLI's own accepted values).
 * @throws std::runtime_error with a Windows-only explanation if this binary
 *         is a non-Windows build/platform and `value` is one of the three
 *         Windows backend values.
 */
void ValidateWindowsSecretBackendPlatform(std::string_view value);
```

Called from CLI wiring (Subtask 02) before/alongside
`ParseWindowsSecretBackend`, so an unrecognized-on-this-platform value fails
with a platform-specific message rather than the generic "unknown value"
message from Subtask 01.

## Error handling (plan.md C4, C5)

- Linux backend requested on Windows: exit non-zero, explain the valid
  Windows values (`dpapi-user`, `dpapi-machine`, `wincred`) - do not merely
  say "invalid value."
- Windows backend requested on a non-Windows platform/build: exit non-zero,
  explain that the backend is Windows-only.
- Non-Windows builds must either compile out the Windows backend code path
  entirely (preferred, via existing platform `#ifdef`/CMake conventions) or
  reject Windows backend values deterministically at runtime if compiled in.

## Constraints

- Must not depend on any Windows-only header/API when compiled for a
  non-Windows platform - platform detection itself must be
  build-portable (`#ifdef _WIN32` or the project's existing platform macro).
- Do not duplicate the list of Linux backend names here if the Linux selector
  already exposes them; reference the Linux selector's known-values list
  instead of hardcoding a second copy that could drift.

## Success criteria

- [ ] A Linux backend name (matching the Linux CLI's own accepted values)
      passed to `--backend` on a Windows build exits non-zero with a message
      naming the three valid Windows values.
- [ ] A Windows backend value passed to `--backend` on a non-Windows
      build/platform exits non-zero with a message stating the backend is
      Windows-only.
- [ ] On a non-Windows build, no Windows-only API (DPAPI, Credential Manager)
      is referenced by the compiled binary.
