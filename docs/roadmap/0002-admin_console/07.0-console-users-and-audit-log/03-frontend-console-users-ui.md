# 03 - Frontend Console Users UI

**Parent task:** 07.0 Console Users and Audit Log
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** 05

## Objective

Build `ConsoleUsersPage` and its create/reset-password/disable dialogs.

## Files to add

```text
web/admin/src/api/consoleUsersApi.ts
web/admin/src/model/consoleUser.ts
web/admin/src/pages/ConsoleUsersPage.tsx
web/admin/src/components/admin/CreateConsoleUserDialog.tsx
web/admin/src/components/admin/ResetPasswordDialog.tsx
web/admin/src/components/admin/DisableUserDialog.tsx
web/admin/src/components/admin/RoleBadge.tsx
```

## Requirements

1. `ConsoleUsersPage` at `/admin/console-users`, visible only to admins (
   `PermissionGate` on `console_users:manage`, and the nav item itself
   hidden for read-only per task 01.0 subtask 03).
2. `PaginatedTable` (task 02.0) with columns `Username, Role, Created At,
   Last Login, Status, Actions`.
3. `CreateConsoleUserDialog`, `ResetPasswordDialog`, `DisableUserDialog` each
   use React Hook Form + Zod for validation (
   [00-decision-record.md §1](/docs/roadmap/0002-admin_console/00-decision-record.md)),
   require CSRF (via `apiClient`), and invalidate the
   `['consoleUsers', ...]` query on success.
4. `RoleBadge` renders role visually consistent with `RiskBadge`/
   `StatusBadge` (task 03.0) styling conventions.
5. Role-change is done inline or via a dialog - implementer's choice, but it
   must go through `PATCH /console-users/{username}` and be reflected
   immediately in the table after success.

## Constraints

- Never render or accept a password value outside `ResetPasswordDialog`'s
  own flow; the reset flow must clearly communicate whether it generates a
  temporary password or accepts an admin-supplied one, matching whatever
  `ConsoleUsersController` (task 07.0 subtask 01) implements.

## Success criteria

- [ ] Admin can create a console user via the dialog.
- [ ] Admin can disable a console user via the dialog with confirmation.
- [ ] Admin can reset a password via the dialog.
- [ ] Admin can change a user's role and see it reflected in the table.
- [ ] Read-only user does not see the Console Users nav item or page.
