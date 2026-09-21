# 03 - Tests

**Parent task:** 03.0 Analytics Dashboard
**State:** ⬜ Not started
**Depends on:** 02
**Blocks:** none

## Objective

Cover the analytics-overview backend service/endpoint and the dashboard
frontend, including the empty-database zero-state path explicitly called
out in the exit criteria.

## Files to add

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

## Requirements

1. `AnalyticsQueryServiceTest` covers totals/series computation against
   fixture redirect events, including at least one suspicious/bot event
   case (even if the risk fields are populated only via a fixture, not the
   real `SuspicionAnalyzer` which lands in task 06.0).
2. `AnalyticsOverviewApiTest` is an integration test against the real admin
   router with seeded data, asserting the full response shape and permission
   enforcement (`analytics:read` required, anonymous/unauthorized rejected).
3. `AnalyticsOverviewEmptyDbTest` is an integration test against an empty
   database asserting zero totals and empty series with a `200`, not an
   error.
4. `DashboardPage.test.tsx` renders against MSW-mocked overview data
   (reusing `web/admin/src/test/msw/handlers.ts` from task 01.0 subtask 04,
   extended with overview handlers) and asserts widgets/charts render.
5. `MetricCard.test.tsx` and `TimeSeriesChart.test.tsx` are focused unit
   tests for those two components in isolation.

## Constraints

- The empty-DB test is a named exit criterion in the source task spec -
  do not fold it into the populated-DB integration test; keep it a separate
  test file so CI reports it distinctly.

## Success criteria

- [ ] All backend tests above pass under CTest, including the empty-database
      case.
- [ ] All frontend tests above pass under `npm run test`.
- [ ] Anonymous user cannot access the dashboard API (asserted by
      `AnalyticsOverviewApiTest`).
- [ ] Dashboard API response is covered by integration tests for both
      populated and empty databases.
