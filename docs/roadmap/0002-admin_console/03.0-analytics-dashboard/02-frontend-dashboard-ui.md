# 02 - Frontend Dashboard UI

**Parent task:** 03.0 Analytics Dashboard
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** 03

## Objective

Replace the task 01.0 `DashboardPage` placeholder with the real
analytics-first dashboard, plus the analytics-overview page, backed by
`GET /admin/api/v1/analytics/overview`.

## Files to add

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

## Requirements

1. `analyticsApi.ts` calls the overview endpoint through the shared
   `apiClient` (task 01.0) using `PageRequest`/`TimeRange` query shapes
   (task 02.0); query key `['analytics', 'overview', filters]` with a 30s
   stale time (
   [02-react-frontend-architecture.md §5](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md)).
2. Dashboard widgets (
   [01-product-and-ux-spec.md §6.1](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)):

   ```text
   Total redirects, Successful redirects, Failed redirects,
   Unique visitor profiles, Unique fingerprints, Unique IPs,
   Active URL pairs, Suspicious events, Bot-like events,
   Fingerprint regeneration attempts
   ```

3. Dashboard charts (
   [01-product-and-ux-spec.md §6.2](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)):

   ```text
   Redirects over time, Successful vs failed redirects,
   Suspicious events over time, New vs returning visitors,
   Top URL pairs, Top referrers, Top suspicious IPs/fingerprints
   ```

4. `TimeSeriesChart` and `TopNBarChart`-equivalent (`TopListCard`) are the
   only two ECharts wrappers required for this MVP page (
   [02-react-frontend-architecture.md §7](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md)) -
   do not call ECharts directly from `DashboardPage`.
5. `AnalyticsOverviewPage` at `/admin/analytics/overview` covers the
   required sections from
   [01-product-and-ux-spec.md §7.1](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md):
   traffic volume, unique identity counts, top links, top referrers, top
   target domains, browser/device distribution, security summary, recent
   suspicious events.
6. Date range selector (from task 02.0's `DateRangePicker`) controls both
   pages' queries; loading/empty/error states use the task 01.0/02.0 shared
   primitives.
7. Metrics show comparison with the previous equivalent period when the
   backend response supports it (optional graceful degradation if not).
8. Charts must be responsive (no fixed pixel widths breaking on narrow
   viewports).

## Constraints

- Confirm `/admin/dashboard` remains the default post-login redirect (set in
  task 01.0 subtask 03) - do not change it to `/admin/url-pairs`.
- Do not fetch `/admin/api/v1/analytics/overview` for a user lacking
  `analytics:read` - `PermissionGate`/route guard first.

## Success criteria

- [ ] Dashboard loads after login with the required widgets and charts.
- [ ] Date range changes refetch dashboard data (query key includes
      filters).
- [ ] Empty database renders zero metrics without crashing or showing an
      error state.
- [ ] Read-only user can view the dashboard.
- [ ] Charts render responsively across at least a desktop and a narrow
      viewport.
