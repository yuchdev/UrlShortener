# 01 - Backend Analytics Overview

**Parent task:** 03.0 Analytics Dashboard
**State:** ⬜ Not started
**Depends on:** none (builds on task 02.0's query primitives)
**Blocks:** 02

## Objective

Implement the `GET /admin/api/v1/analytics/overview` endpoint: totals,
time-series, and the query service/repository backing the dashboard and
analytics-overview page.

## Files to add

```text
src/admin/controllers/AnalyticsController.h
src/admin/controllers/AnalyticsController.cpp
src/admin/analytics/AnalyticsQueryService.h
src/admin/analytics/AnalyticsQueryService.cpp
src/admin/analytics/AnalyticsOverviewDto.h
src/admin/analytics/AnalyticsOverviewDto.cpp
src/admin/analytics/AnalyticsTimeseriesDto.h
src/admin/analytics/AnalyticsTimeseriesDto.cpp
src/admin/analytics/AnalyticsTopListDto.h
src/admin/analytics/AnalyticsTopListDto.cpp
src/admin/storage/AnalyticsRepository.h
src/admin/storage/AnalyticsRepository.cpp
```

## API contract

```http
GET /admin/api/v1/analytics/overview
```

Query: `from`, `to`, `granularity`, `timezone` (via `TimeRange` from task
02.0). Response shape (
[04-admin-api-contract.md §4.1](/docs/roadmap/0002-admin_console/04-admin-api-contract.md)):

```json
{
  "range": { "from": "...", "to": "...", "granularity": "hour" },
  "totals": {
    "redirects": 0, "successful_redirects": 0, "failed_redirects": 0,
    "unique_visitor_profiles": 0, "unique_fingerprints": 0, "unique_ips": 0,
    "active_url_pairs": 0, "suspicious_events": 0, "bot_events": 0,
    "tampering_events": 0, "fingerprint_regeneration_attempts": 0
  },
  "series": { "redirects": [{ "bucket": "...", "count": 0 }], "suspicious_events": [] }
}
```

## Requirements

1. Query the immutable redirect-event stream or the `analytics_redirects_by_*`
   aggregate tables (
   [05-data-model-and-storage.md §2, §8](/docs/roadmap/0002-admin_console/05-data-model-and-storage.md)) -
   never derive totals from mutable URL pair rows.
2. Return correct zero-valued totals/empty series when the database is
   empty - do not error or return null fields.
3. Use the indexes defined in
   [05-data-model-and-storage.md §11](/docs/roadmap/0002-admin_console/05-data-model-and-storage.md)
   (`redirect_event(timestamp)`, `redirect_event(risk_level, timestamp)`,
   etc.) or aggregate tables for large datasets - do not full-scan
   `redirect_event`.
4. Enforce `analytics:read` permission via `AdminPermissionMiddleware` (task
   01.0 subtask 02).
5. Mask sensitive values (e.g. any IP/fingerprint identifiers surfaced in
   top-list DTOs) according to the current user's permissions (
   [plan.md - Shared contract C5](/docs/roadmap/0002-admin_console/plan.md));
   full masking-service integration lands in task 08.0, so this subtask
   should route masking decisions through a narrow interface task 08.0 can
   extend rather than hard-coding "show everything."

## Constraints

- `bot_events`, `tampering_events`, and `fingerprint_regeneration_attempts`
  totals will read as zero until task 06.0's `SuspicionAnalyzer` populates
  `fingerprint_signal`/`fingerprint_regeneration_event` rows - the query
  must still be correct (sum of zero rows), not stubbed with a hardcoded
  constant.

## Success criteria

- [ ] `GET /admin/api/v1/analytics/overview` returns the documented shape
      for a populated database.
- [ ] The same endpoint returns correct zero totals and empty series for an
      empty database.
- [ ] `analytics:read` is enforced; a request without it returns
      `not_authorized` in the common error format.
- [ ] Query plan uses an index or aggregate table, not a full scan, for the
      time-bucketed series (verified in `AnalyticsQueryServiceTest`).
