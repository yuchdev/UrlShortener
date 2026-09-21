# 02 - Backend Export Service

**Parent task:** 08.0 Exports, Privacy, and Permissions
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** 03

## Objective

Implement the export endpoints, export-policy enforcement, and export-job
lifecycle (creation, retrieval, expiration).

## Files to add

```text
src/admin/controllers/ExportController.h
src/admin/controllers/ExportController.cpp
src/admin/export/ExportService.h
src/admin/export/ExportService.cpp
src/admin/export/ExportPolicy.h
src/admin/export/ExportPolicy.cpp
src/admin/dto/ExportDto.h
src/admin/dto/ExportDto.cpp
```

## API contract

```http
POST /admin/api/v1/exports/analytics
POST /admin/api/v1/exports/url-pairs
POST /admin/api/v1/exports/redirect-events
GET  /admin/api/v1/exports/{export_id}
```

## Export types

```text
aggregate_analytics_csv
aggregate_analytics_json
url_pairs_csv
redirect_events_csv
redirect_events_json
```

## Permission rules

```text
readonly:
  may export aggregate analytics if exports:create_aggregate exists
  cannot export raw redirect events
  cannot export full IP/fingerprint payloads

admin:
  may export aggregate analytics
  may export URL pairs
  may export raw redirect events only if exports:create_raw exists and policy allows it
```

## Requirements

1. `ExportPolicy` implements the permission-rule table above, consulted by
   `ExportController` before `ExportService` does any work.
2. Every export applies `MaskingService` (subtask 01) to its rows before
   serialization - raw fingerprint components excluded by default; full IPs
   included only if role/permission/policy all allow it (
   [plan.md - Shared contract C5](/docs/roadmap/0002-admin_console/plan.md)).
3. Every export request and completion is audit-logged via task 07.0's
   `AuditLogger` (
   [plan.md - Shared contract C7](/docs/roadmap/0002-admin_console/plan.md)).
4. Export files expire: recommended 24h local/dev, 1-7 days production
   depending on policy - implement as a configurable retention setting, not
   a hardcoded constant.
5. `GET /exports/{export_id}` returns job status/download info; do not
   return raw file bytes through this JSON endpoint if the project's export
   mechanism is file-based (design a separate download path if needed, or
   document the chosen approach in this subtask's implementation).

## Constraints

- `ExportService` must reuse `AnalyticsRepository`/`UrlPairsAdminRepository`
  (tasks 03.0/04.0) and `MaskingService` (subtask 01) rather than
  re-querying storage directly with export-specific logic.

## Success criteria

- [ ] Aggregate analytics export works for both admin and (if permitted)
      read-only.
- [ ] URL pairs export works for admin.
- [ ] Raw redirect-event export is admin-only and additionally
      `exports:create_raw`-gated.
- [ ] Exported IP/fingerprint data is masked identically to the live API for
      the same requesting user.
- [ ] Export files/jobs expire per the configured retention window.
- [ ] Every export request and completion appears in the audit log.
