# Task 07 — Console Users and Audit Log

## Goal

Implement management UI/API for console users and a searchable audit log.

This task is for people who log into the admin console, not external visitor profiles.

## Required frontend files

Create:

```text
web/admin/src/api/consoleUsersApi.ts
web/admin/src/api/auditLogApi.ts
web/admin/src/model/consoleUser.ts
web/admin/src/model/auditLog.ts
web/admin/src/pages/ConsoleUsersPage.tsx
web/admin/src/pages/AuditLogPage.tsx
web/admin/src/components/admin/CreateConsoleUserDialog.tsx
web/admin/src/components/admin/ResetPasswordDialog.tsx
web/admin/src/components/admin/DisableUserDialog.tsx
web/admin/src/components/admin/RoleBadge.tsx
web/admin/src/components/audit/AuditLogTable.tsx
```

## Required backend files

Create or extend:

```text
src/admin/controllers/ConsoleUsersController.h
src/admin/controllers/ConsoleUsersController.cpp
src/admin/controllers/AuditLogController.h
src/admin/controllers/AuditLogController.cpp
src/admin/storage/ConsoleUsersRepository.h
src/admin/storage/ConsoleUsersRepository.cpp
src/admin/storage/AuditLogRepository.h
src/admin/storage/AuditLogRepository.cpp
src/admin/audit/AuditLogger.h
src/admin/audit/AuditLogger.cpp
src/admin/dto/ConsoleUserDto.h
src/admin/dto/ConsoleUserDto.cpp
src/admin/dto/AuditLogDto.h
src/admin/dto/AuditLogDto.cpp
```

## Required endpoints

```http
GET   /admin/api/v1/console-users
POST  /admin/api/v1/console-users
GET   /admin/api/v1/console-users/{username}
PATCH /admin/api/v1/console-users/{username}
POST  /admin/api/v1/console-users/{username}/reset-password
POST  /admin/api/v1/console-users/{username}/disable
GET   /admin/api/v1/audit-log
```

## Console users table columns

```text
Username
Role
Created At
Last Login
Status
Actions
```

## Audit log table columns

```text
Time
Console User
Action
Target Type
Target ID
Source IP
Result
```

## Requirements

1. Only admins can manage console users.
2. Read-only users cannot see Console Users page.
3. Audit Log page requires `audit_log:read`.
4. User creation writes audit event.
5. User disable writes audit event.
6. Password reset writes audit event.
7. Role change writes audit event.
8. Permission-denied attempts write audit event.

## Tests to add

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

## Exit criteria

- Admin can create a console user.
- Admin can disable a console user.
- Admin can reset password.
- Admin can change role.
- Read-only cannot access console user management.
- Audit log records all sensitive actions.
- Audit log is searchable and paginated.
