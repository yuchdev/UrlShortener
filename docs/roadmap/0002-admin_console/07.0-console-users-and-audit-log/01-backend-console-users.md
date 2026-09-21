# 01 - Backend Console Users

**Parent task:** 07.0 Console Users and Audit Log
**State:** ⬜ Not started
**Depends on:** none (builds on task 02.0's query primitives)
**Blocks:** 03

## Objective

Implement console-user management: list, create, get, update/role-change,
reset-password, and disable.

## Files to add

```text
src/admin/controllers/ConsoleUsersController.h
src/admin/controllers/ConsoleUsersController.cpp
src/admin/storage/ConsoleUsersRepository.h
src/admin/storage/ConsoleUsersRepository.cpp
src/admin/dto/ConsoleUserDto.h
src/admin/dto/ConsoleUserDto.cpp
```

## API contract

```http
GET   /admin/api/v1/console-users
POST  /admin/api/v1/console-users
GET   /admin/api/v1/console-users/{username}
PATCH /admin/api/v1/console-users/{username}
POST  /admin/api/v1/console-users/{username}/reset-password
POST  /admin/api/v1/console-users/{username}/disable
```

All require `console_users:manage` (
[04-admin-api-contract.md §6](/docs/roadmap/0002-admin_console/04-admin-api-contract.md)).

## Requirements

1. List columns (
   [01-product-and-ux-spec.md §8.5](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)):
   `Username, Role, Created At, Last Login, Status, Actions`.
2. Only admins can manage console users; every endpoint in this subtask
   rejects a caller lacking `console_users:manage` with `not_authorized`.
3. `POST /console-users` creates a user (username, initial role, initial
   credential handling delegated to the existing safe-login credential
   store from task 01.0 - do not reimplement password hashing here).
4. `PATCH /console-users/{username}` supports role change.
5. `POST .../reset-password` and `POST .../disable` perform their named
   action.
6. `ConsoleUserDto` never includes a password hash or raw credential
   material.

## Constraints

- This subtask does not implement audit logging itself - subtask 02's
  `AuditLogger` is what tasks 07.0's controllers call into; stub the call
  sites here behind the same narrow interface task 01.0/04.0 used, so
  subtask 02 can wire the real implementation without touching this
  controller's logic.

## Success criteria

- [ ] Admin can create, list, and view console users.
- [ ] Admin can change a user's role.
- [ ] Admin can reset a user's password.
- [ ] Admin can disable a user.
- [ ] Read-only caller receives `not_authorized` for every endpoint in this
      subtask.
- [ ] No response ever includes a password hash or raw credential.
