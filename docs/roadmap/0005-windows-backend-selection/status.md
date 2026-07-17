# Windows secret-store backend selection status

## Current status

Status: planned.

This roadmap item is now Windows-focused. The target deliverable is explicit
backend selection for `secret-store.exe` on Windows, with the same user-facing
control that Linux already provides.

## Required Windows backend values

| Status | CLI value | Backend |
|---|---|---|
| Planned | `dpapi-user` | DPAPI CurrentUser |
| Planned | `dpapi-machine` | DPAPI LocalMachine |
| Planned | `wincred` | Windows Credential Manager |

## Completed

- Re-scoped this roadmap item from Linux backend selection to Windows backend
  selection.
- Defined the three explicit Windows backend values.
- Defined the expected CLI behavior and failure modes.
- Defined test coverage expectations for parser, factory, and command-level
  behavior.

## Not started

- Add `--backend` parsing for `secret-store.exe` on Windows.
- Add Windows backend enum and parser.
- Add Windows secret-store factory selection.
- Wire `dpapi-user`, `dpapi-machine`, and `wincred` adapters into the factory.
- Add fake-adapter unit tests for all three backend values.
- Add Windows-only integration tests for real DPAPI/Credential Manager behavior,
  if the project wants opt-in host-level validation.
- Update user-facing CLI documentation.

## Design decisions

- Windows supports exactly three explicit backend values:
  `dpapi-user`, `dpapi-machine`, and `wincred`.
- A requested backend must never silently fall back to a different backend.
- `dpapi-user` should remain the safest default for interactive Windows use.
- `dpapi-machine` is for service scenarios and must be documented as broader
  trust scope.
- `wincred` is selected only when the caller explicitly wants Windows Credential
  Manager semantics.

## Risks

| Risk | Mitigation |
|---|---|
| Accidentally accepting Linux backend names on Windows | Platform-specific validation and tests |
| Silent fallback hides misconfiguration | Treat unsupported backend as fatal |
| LocalMachine DPAPI broadens access unexpectedly | Document ACL requirements and service-only use case |
| Tests modify real user Credential Manager entries | Use fake adapters by default; make real integration tests opt-in |
| Secrets appear in diagnostics | Assert test logs and error paths do not include secret payloads |

## Next implementation steps

1. Add a Windows backend parser for `dpapi-user`, `dpapi-machine`, and `wincred`.
2. Add factory tests proving each value selects the correct adapter.
3. Wire the parser into `secret-store.exe --backend`.
4. Preserve existing automatic selection when `--backend` is omitted.
5. Add documentation examples for all three Windows values.

## Validation checklist

- `secret-store.exe --backend dpapi-user ...` selects DPAPI CurrentUser.
- `secret-store.exe --backend dpapi-machine ...` selects DPAPI LocalMachine.
- `secret-store.exe --backend wincred ...` selects Windows Credential Manager.
- Invalid values fail with valid Windows values in the error.
- Linux backend names are rejected on Windows.
- No secret material is logged or printed during backend selection failures.
