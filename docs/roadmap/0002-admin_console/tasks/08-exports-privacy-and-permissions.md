# Task 08 — Exports, Privacy, and Permissions

## Goal

Implement controlled exports and enforce privacy/masking policies across admin APIs.

## Required frontend files

Create:

```text
web/admin/src/api/exportsApi.ts
web/admin/src/model/exportJob.ts
web/admin/src/components/admin/ExportButton.tsx
web/admin/src/components/admin/ExportDialog.tsx
web/admin/src/components/privacy/PrivacyNotice.tsx
web/admin/src/pages/SettingsPage.tsx
```

## Required backend files

Create:

```text
src/admin/controllers/ExportController.h
src/admin/controllers/ExportController.cpp
src/admin/export/ExportService.h
src/admin/export/ExportService.cpp
src/admin/export/ExportPolicy.h
src/admin/export/ExportPolicy.cpp
src/admin/privacy/PrivacyPolicy.h
src/admin/privacy/PrivacyPolicy.cpp
src/admin/privacy/MaskingService.h
src/admin/privacy/MaskingService.cpp
src/admin/dto/ExportDto.h
src/admin/dto/ExportDto.cpp
```

## Required endpoints

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

## Privacy rules

1. All exports must apply the same masking rules as APIs.
2. Raw fingerprint components are excluded by default.
3. Full IPs are exported only if role, permission, and policy allow it.
4. Every export is audit-logged.
5. Export files should expire.

Recommended expiration:

```text
24 hours local/dev
1-7 days production depending on policy
```

## Tests to add

Frontend:

```text
web/admin/src/components/admin/ExportDialog.test.tsx
web/admin/src/components/admin/ExportButton.test.tsx
```

Backend:

```text
tests/unit/admin/ExportPolicyTest.cpp
tests/unit/admin/MaskingServiceTest.cpp
tests/unit/admin/PrivacyPolicyTest.cpp
tests/integration/admin/ExportApiTest.cpp
tests/integration/admin/ReadOnlyCannotExportRawEventsTest.cpp
tests/integration/admin/ExportAuditLogTest.cpp
```

## Exit criteria

- Aggregate analytics export works.
- URL pairs export works for admin.
- Raw redirect event export is admin/policy controlled.
- Read-only user cannot export raw events.
- Exported IP/fingerprint data is masked as required.
- Every export action is visible in audit log.
