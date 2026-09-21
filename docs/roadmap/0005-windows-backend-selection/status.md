# Milestone 0005 - Windows Backend Selection - Status

Tracks progress against
[plan.md](/docs/roadmap/0005-windows-backend-selection/plan.md).

## Current status

| Task | Name | Status | Tests |
|------|------|--------|-------|
| 01.0 | [CLI backend flag parsing & validation](/docs/roadmap/0005-windows-backend-selection/01.0-cli-backend-flag-parsing-and-validation/README.md) | ⬜ Not started | 0 |
| 02.0 | [DPAPI CurrentUser adapter](/docs/roadmap/0005-windows-backend-selection/02.0-dpapi-current-user-adapter/README.md) | ⬜ Not started | 0 |
| 03.0 | [DPAPI LocalMachine adapter](/docs/roadmap/0005-windows-backend-selection/03.0-dpapi-local-machine-adapter/README.md) | ⬜ Not started | 0 |
| 04.0 | [Windows Credential Manager adapter](/docs/roadmap/0005-windows-backend-selection/04.0-windows-credential-manager-adapter/README.md) | ⬜ Not started | 0 |
| 05.0 | [Factory wiring & auto-selection](/docs/roadmap/0005-windows-backend-selection/05.0-factory-wiring-and-auto-selection/README.md) | ⬜ Not started | 0 |
| 06.0 | [Test suite](/docs/roadmap/0005-windows-backend-selection/06.0-test-suite/README.md) | ⬜ Not started | 0 |
| 07.0 | [Documentation](/docs/roadmap/0005-windows-backend-selection/07.0-documentation/README.md) | ⬜ Not started | 0 |

**Legend:** ✅ Complete · 🔶 In progress / partial · ⬜ Not started

**Current gate status:** 0% implemented - no code for any task exists yet.
**Blocking open question:** a repo-wide search found no `secret-store.exe`,
`ISecretStore`, or Linux backend selector anywhere in this checkout (see the
verification note in [plan.md](/docs/roadmap/0005-windows-backend-selection/plan.md)).
Confirm where/whether the Linux implementation this milestone is meant to
match actually lives before starting Task 01.0.

## Notes & decisions

### Design decisions

- Windows supports exactly three explicit backend values: `dpapi-user`,
  `dpapi-machine`, and `wincred` (Tasks 02.0-04.0).
- A requested backend must never silently fall back to a different backend
  (plan.md C2).
- `dpapi-user` should remain the safest default for interactive Windows use
  (Task 02.0, wired through auto-selection in Task 05.0).
- `dpapi-machine` is for service scenarios and must be documented as broader
  trust scope (Task 03.0, documented in Task 07.0).
- `wincred` is selected only when the caller explicitly wants Windows
  Credential Manager semantics (Task 04.0).

### Risks

| Risk | Mitigation | Owning task(s) |
|---|---|---|
| Accidentally accepting Linux backend names on Windows | Platform-specific validation and tests | 01.0, 06.0 |
| Silent fallback hides misconfiguration | Treat unsupported backend as fatal (plan.md C2) | 01.0 |
| LocalMachine DPAPI broadens access unexpectedly | Document ACL requirements and service-only use case | 03.0, 07.0 |
| Tests modify real user Credential Manager entries | Use fake adapters by default; make real integration tests opt-in | 06.0 |
| Secrets appear in diagnostics | Assert test logs and error paths do not include secret payloads (plan.md C3) | 05.0, 06.0 |
| Linux `secret-store` implementation this milestone should match cannot be located in this repo | Confirm its actual location/name with the team before Task 01.0 starts, or scope this milestone as introducing `ISecretStore` from scratch | 01.0 |

## Decomposition tree (as built)

```text
docs/roadmap/0005-windows-backend-selection/
  plan.md
  status.md
  01.0-cli-backend-flag-parsing-and-validation/
    README.md
    01-windows-secret-backend-enum-and-parser.md
    02-backend-flag-cli-wiring-and-error-messages.md
    03-platform-validation-linux-and-non-windows-rejection.md
  02.0-dpapi-current-user-adapter/
    README.md
    01-dpapi-current-user-protect-and-unprotect.md
    02-dpapi-current-user-isecretstore-conformance.md
  03.0-dpapi-local-machine-adapter/
    README.md
    01-dpapi-local-machine-protect-and-unprotect.md
    02-dpapi-local-machine-acl-and-scope-documentation-notes.md
  04.0-windows-credential-manager-adapter/
    README.md
    01-wincred-target-naming-and-crud-preservation.md
    02-wincred-isecretstore-conformance.md
  05.0-factory-wiring-and-auto-selection/
    README.md
    01-build-windows-secret-store-factory.md
    02-omitted-backend-auto-selection-and-diagnostics.md
  06.0-test-suite/
    README.md
    01-parser-and-validation-unit-tests.md
    02-factory-adapter-selection-unit-tests.md
    03-secret-safety-assertions.md
    04-opt-in-windows-integration-tests.md
  07.0-documentation/
    README.md
    01-windows-backend-table-and-dpapi-tradeoffs.md
    02-wincred-naming-and-migration-note.md
```
