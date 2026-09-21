# 04 - Frontend Audit Log UI

**Parent task:** 07.0 Console Users and Audit Log
**State:** ⬜ Not started
**Depends on:** 02
**Blocks:** 05

## Objective

Build `AuditLogPage` and its table, searchable by every filter the backend
supports.

## Files to add

```text
web/admin/src/api/auditLogApi.ts
web/admin/src/model/auditLog.ts
web/admin/src/pages/AuditLogPage.tsx
web/admin/src/components/audit/AuditLogTable.tsx
```

## Requirements

1. `AuditLogPage` at `/admin/audit-log`, requires `audit_log:read` (
   [01-product-and-ux-spec.md §8.6](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)).
2. `AuditLogTable` built on `PaginatedTable` (task 02.0) with columns
   `Time, Console User, Action, Target Type, Target ID, Source IP, Result`.
3. Filter controls for `from, to, console_user, action, target_type,
   target_id, result`, reflected in URL search params via
   `useSearchParamsState` (task 02.0).
4. Query key `['auditLog', filters, pagination]` (
   [02-react-frontend-architecture.md §5](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md)).

## Constraints

- `Source IP` column renders the hashed value the backend returns - never
  attempt to reverse or display a raw IP here (audit log IPs are hashed by
  design per task 07.0 subtask 02).

## Success criteria

- [ ] Audit log is paginated and searchable by every documented filter.
- [ ] A user without `audit_log:read` cannot reach the page (redirect or
      not-authorized state).
- [ ] Filter state is reflected in URL search params.
