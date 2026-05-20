# Task 03 — Analytics Dashboard

## Goal

Implement the analytics-first dashboard using real backend data.

Default route after login must be `/admin/dashboard`.

## Required frontend files

Create:

```text
web/admin/src/api/analyticsApi.ts
web/admin/src/model/analytics.ts
web/admin/src/components/analytics/MetricCard.tsx
web/admin/src/components/analytics/TimeSeriesChart.tsx
web/admin/src/components/analytics/TopListCard.tsx
web/admin/src/components/analytics/RiskBadge.tsx
web/admin/src/components/analytics/StatusBadge.tsx
web/admin/src/pages/DashboardPage.tsx
web/admin/src/pages/AnalyticsOverviewPage.tsx
```

## Required backend files

Create:

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

## Required endpoint

```http
GET /admin/api/v1/analytics/overview
```

## Dashboard metrics

Implement:

```text
total redirects
successful redirects
failed redirects
unique visitor profiles
unique fingerprints
unique IPs
active URL pairs
suspicious events
bot-like events
tampering events
fingerprint regeneration attempts
```

## Dashboard charts

Implement:

```text
redirects over time
suspicious events over time
top URL pairs
top referrers
top suspicious IPs/fingerprints summary
```

## Backend requirements

1. Query immutable redirect events or aggregate tables.
2. Return zero-state data when DB is empty.
3. Use indexes or aggregate tables for large datasets.
4. Enforce `analytics:read` permission.
5. Mask sensitive values according to current user's permissions.

## Frontend requirements

1. Date range selector controls dashboard query.
2. Loading state is visible.
3. Empty state is useful.
4. Error state is useful.
5. Metrics show comparison with previous equivalent period if backend supports it.
6. Charts must be responsive.

## Tests to add

Frontend:

```text
web/admin/src/pages/DashboardPage.test.tsx
web/admin/src/components/analytics/MetricCard.test.tsx
web/admin/src/components/analytics/TimeSeriesChart.test.tsx
```

Backend:

```text
tests/unit/admin/AnalyticsQueryServiceTest.cpp
tests/integration/admin/AnalyticsOverviewApiTest.cpp
tests/integration/admin/AnalyticsOverviewEmptyDbTest.cpp
```

## Exit criteria

- Dashboard loads after login.
- Date range changes refetch dashboard data.
- Empty database displays zero metrics without crashing.
- Read-only user can view dashboard.
- Anonymous user cannot access dashboard API.
- Dashboard API response is covered by integration tests.
