# Task 07.0 - Console Users and Audit Log

**Parent milestone:** [Milestone 0002 - Admin Console](/docs/roadmap/0002-admin_console/plan.md)
**Status:** ⬜ Not started

## Scope

Implement management UI/API for console users (people who log into the
admin console - not external visitor profiles) and a searchable audit log.
This task also supplies the real `AuditLogger` that tasks 01.0 and 04.0
stubbed calls toward.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Backend console users](01-backend-console-users.md) | ⬜ Not started | 03 |
| 02 | [Backend audit log](02-backend-audit-log.md) | ⬜ Not started | 04 |
| 03 | [Frontend console users UI](03-frontend-console-users-ui.md) | ⬜ Not started | 05 |
| 04 | [Frontend audit log UI](04-frontend-audit-log-ui.md) | ⬜ Not started | 05 |
| 05 | [Tests](05-tests.md) | ⬜ Not started | none |

## Key constraints

- Depends on task 01.0 (auth) and task 02.0 (pagination). Subtask 02
  (`AuditLogger`) must be wired back into task 01.0 subtask 02's login/
  logout audit calls and task 04.0 subtask 01's disable-URL-pair audit call,
  replacing whatever storage-backend stub those tasks used.
- Only admins can manage console users; read-only users cannot see the
  Console Users page or call its endpoints (
  [plan.md - Shared contract C6](/docs/roadmap/0002-admin_console/plan.md)).
- Every sensitive action (user creation, disable, password reset, role
  change, permission-denied attempts) is audit-logged (
  [plan.md - Shared contract C7](/docs/roadmap/0002-admin_console/plan.md)).
