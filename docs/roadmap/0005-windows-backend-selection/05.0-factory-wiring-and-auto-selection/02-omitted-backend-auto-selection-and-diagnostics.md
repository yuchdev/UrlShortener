# 02 - Omitted-backend auto-selection and diagnostics

**Parent task:** 05.0 Factory wiring & auto-selection
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** none

## Objective

When `--backend` is omitted, preserve Windows' existing auto-selection
behavior and report the selected backend in non-secret diagnostic output
wherever the command already prints backend information (plan.md C1).

## Files

- `src/secret_store/main_secret_store.cpp` (resolve the
  `std::optional<WindowsSecretBackend>` from Task 01.0 Subtask 02 into a
  concrete backend when empty)
- `src/secret_store/windows/windows_secret_store_factory.cpp` (if the
  default-selection logic lives alongside the factory rather than in `main`)

## Behavior (plan.md CLI contract + Backend semantics)

- If `--backend` is omitted, Windows may keep its existing auto-selection
  behavior. In the absence of a pre-existing auto-selection mechanism to
  preserve, default to `WindowsSecretBackend::dpapi_user` - `dpapi-user`
  is documented as "the default backend for interactive use" and "the
  safest default for interactive Windows use" (plan.md Backend semantics,
  status.md Design decisions).
- The selected backend (whether explicit or auto-selected) must be reported
  in non-secret diagnostic output whenever the command already prints
  backend information - e.g. a `--verbose`/status/diagnostic command that
  already names the active metadata/cache backend should also name the
  active secret-store backend. This must never include secret values, only
  the backend name (`dpapi-user` / `dpapi-machine` / `wincred`).
- Auto-selection must never partially apply - either a backend is fully
  resolved before any command executes, or the process exits non-zero
  (plan.md C2).

## Constraints

- Auto-selection resolution happens once per process invocation, not
  per-command, to keep behavior predictable within a single run.
- Diagnostic output reporting the selected backend must use `ToCliValue`
  (Task 01.0 Subtask 01) rather than a second string mapping.

## Success criteria

- [ ] `secret-store.exe <command>` (no `--backend`) resolves to
      `WindowsSecretBackend::dpapi_user` and behaves identically to
      `secret-store.exe --backend dpapi-user <command>`.
- [ ] Any diagnostic/status command that already prints backend information
      includes the resolved secret-store backend name, with no secret
      values present.
- [ ] Auto-selection failure (if the default backend itself cannot be
      constructed) exits non-zero rather than silently trying another
      backend.
