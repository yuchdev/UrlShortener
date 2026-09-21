# 02 - Backend Audit Log

**Parent task:** 07.0 Console Users and Audit Log
**State:** ⬜ Not started
**Depends on:** none (parallel with subtask 01; both build on task 02.0)
**Blocks:** 04

## Objective

Implement the real `AuditLogger`, its storage repository, and the searchable
`GET /admin/api/v1/audit-log` endpoint - then rewire task 01.0 subtask 02's
login/logout events and task 04.0 subtask 01's disable-URL-pair event to
call through it instead of whatever stub they used.

## Files to add

```text
src/admin/controllers/AuditLogController.h
src/admin/controllers/AuditLogController.cpp
src/admin/storage/AuditLogRepository.h
src/admin/storage/AuditLogRepository.cpp
src/admin/audit/AuditLogger.h
src/admin/audit/AuditLogger.cpp
src/admin/dto/AuditLogDto.h
src/admin/dto/AuditLogDto.cpp
```

## API contract

```http
GET /admin/api/v1/audit-log
```

Filters: `from, to, console_user, action, target_type, target_id, result` (
[04-admin-api-contract.md §7](/docs/roadmap/0002-admin_console/04-admin-api-contract.md)).
Requires `audit_log:read`.

## Requirements

1. `AuditLogger` exposes a simple `record(event)` API that every controller
   in the milestone calls; record fields (
   [06-security-privacy-permissions.md §7](/docs/roadmap/0002-admin_console/06-security-privacy-permissions.md)):

   ```text
   timestamp, console_user, action, target_type, target_id,
   source_ip_hash, result, request_id, metadata_json
   ```

2. Audited events (
   [06-security-privacy-permissions.md §7](/docs/roadmap/0002-admin_console/06-security-privacy-permissions.md)):

   ```text
   login success, login failure, logout, password change,
   console user creation, console user disable, role change,
   permission denied, URL pair mutation, export request,
   export completion, settings change
   ```

3. `GET /audit-log` list columns (
   [01-product-and-ux-spec.md §8.6](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)):
   `Time, Console User, Action, Target Type, Target ID, Source IP, Result`,
   paginated via task 02.0's `PageRequest`.
4. Rewire task 01.0 subtask 02's login-success/login-failure/logout calls
   and task 07.0 subtask 01's console-user create/disable/reset-password/
   role-change calls, and task 04.0 subtask 01's disable-URL-pair call, to
   go through `AuditLogger` (replacing their interim stub).
5. `AuditLogRepository` writes and queries the `audit_log` table using the
   `audit_log(timestamp)` index (
   [05-data-model-and-storage.md §11](/docs/roadmap/0002-admin_console/05-data-model-and-storage.md)).
6. Permission-denied attempts (from `AdminPermissionMiddleware`, task 01.0
   subtask 02) must also call `AuditLogger.record(...)` - wire this in as
   part of this subtask since the middleware predates the logger.

## Constraints

- `source_ip_hash` only - never store or return a raw IP in the audit log
  itself; masking policy (task 08.0) governs what an admin sees elsewhere,
  but the audit log's own IP field is hashed by design.
- Audit writes must not fail the primary action if the audit write itself
  fails - log the audit-write failure through normal application logging
  (never through `AuditLogger` itself, to avoid recursion) rather than
  rolling back or erroring the user-facing request.

## Success criteria

- [ ] `AuditLogger.record(...)` is called from every audited action listed
      above, including permission-denied.
- [ ] `GET /audit-log` is paginated, filterable by every documented field,
      and requires `audit_log:read`.
- [ ] Login/logout events from task 01.0 and the disable-URL-pair event from
      task 04.0 now flow through the real `AuditLogger`.
- [ ] Audit log never stores or returns a raw (unhashed) source IP.
