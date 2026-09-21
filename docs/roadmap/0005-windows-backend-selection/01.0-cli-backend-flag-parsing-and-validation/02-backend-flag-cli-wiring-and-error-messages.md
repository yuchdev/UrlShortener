# 02 - Backend flag CLI wiring and error messages

**Parent task:** 01.0 CLI backend flag parsing & validation
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** none

## Objective

Wire `--backend <value>` into `secret-store.exe`'s Windows CLI entry point
(`src/secret_store/main_secret_store.cpp`) so the option is recognized,
parsed via `ParseWindowsSecretBackend`, and either produces a validated
`WindowsSecretBackend` or fails fast with a clear error.

## CLI contract (plan.md C1)

```powershell
secret-store.exe --backend dpapi-user <command> ...
secret-store.exe --backend dpapi-machine <command> ...
secret-store.exe --backend wincred <command> ...
```

- `--backend` takes exactly one value.
- The accepted values are Windows-specific: `dpapi-user`, `dpapi-machine`,
  `wincred`.
- If `--backend` is omitted entirely, this subtask does nothing further - the
  omitted-flag auto-selection behavior is Task 05.0's responsibility. This
  subtask only needs to leave a `std::optional<WindowsSecretBackend>` (or
  equivalent) that is empty when `--backend` was not passed.

## Error handling

- Unsupported/unrecognized backend value: print the error from
  `ParseWindowsSecretBackend` (which already lists the valid Windows values)
  to stderr and exit non-zero. Do not proceed to command execution.
- `--backend` passed with no value (e.g. trailing flag): treat identically to
  an invalid value - exit non-zero with a usage message that also lists the
  three valid Windows values.
- This subtask does not implement platform validation (Linux name / non-Windows
  rejection) - that is Subtask 03. It does call into the platform-validation
  path so the two compose correctly, but the validation logic itself is owned
  by Subtask 03.

## Files

- `src/secret_store/main_secret_store.cpp` (new `--backend` handling, calling
  `ParseWindowsSecretBackend` from Subtask 01 and the platform check from
  Subtask 03)

## Constraints

- Exit non-zero on any parse/validation failure (plan.md C2 - never silently
  fall back).
- Error text must not echo any part of the process environment or any file
  contents - only the literal `--backend` value the user passed and the
  static list of valid values.

## Success criteria

- [ ] `secret-store.exe --backend dpapi-user <command>` reaches command
      execution with `WindowsSecretBackend::dpapi_user` resolved.
- [ ] `secret-store.exe --backend bogus <command>` exits non-zero and prints
      an error listing `dpapi-user`, `dpapi-machine`, `wincred`.
- [ ] `secret-store.exe --backend <command>` (no value) exits non-zero with a
      usage message.
- [ ] `secret-store.exe <command>` (no `--backend` at all) does not error at
      this layer - it produces an empty/absent backend selection for Task
      05.0 to resolve.
