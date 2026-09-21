# 05 - Tests

**Parent task:** 07.0 Console Users and Audit Log
**State:** ⬜ Not started
**Depends on:** 03, 04
**Blocks:** none

## Objective

Cover console-user management, audit logging, and both frontend pages,
including the read-only-cannot-manage-console-users permission case.

## Files to add

Frontend:

```text
web/admin/src/pages/ConsoleUsersPage.test.tsx
web/admin/src/pages/AuditLogPage.test.tsx
web/admin/src/components/admin/CreateConsoleUserDialog.test.tsx
```

Backend:

```text
tests/unit/admin/AuditLoggerTest.cpp
tests/unit/admin/ConsoleUsersRepositoryTest.cpp
tests/integration/admin/ConsoleUsersApiTest.cpp
tests/integration/admin/AuditLogApiTest.cpp
tests/integration/admin/ReadOnlyCannotManageConsoleUsersTest.cpp
```

## Requirements

1. `AuditLoggerTest` covers `record(...)` producing correctly-shaped audit
   rows for each event type, including permission-denied.
2. `ConsoleUsersRepositoryTest` covers CRUD/role-change/disable at the
   repository layer.
3. `ConsoleUsersApiTest` / `AuditLogApiTest` are integration tests against
   the real admin router.
4. `ReadOnlyCannotManageConsoleUsersTest` is a dedicated integration test
   asserting every console-user endpoint rejects a read-only session with
   `not_authorized`.
5. Frontend tests cover page rendering, the create-user dialog's validation,
   and read-only nav-item hiding.

## Constraints

- Keep the read-only-permission-denial test isolated in its own file,
  matching the source task's explicit test list.

## Success criteria

- [ ] All backend tests above pass under CTest.
- [ ] All frontend tests above pass under `npm run test`.
- [ ] Audit log records all sensitive actions (asserted by
      `AuditLoggerTest` and `AuditLogApiTest` together).
- [ ] Audit log is confirmed searchable and paginated by integration test.
- [ ] Read-only cannot access console-user management (backend-enforced).
