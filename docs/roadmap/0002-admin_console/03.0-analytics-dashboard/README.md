# Task 03.0 - Analytics Dashboard

**Parent milestone:** [Milestone 0002 - Admin Console](/docs/roadmap/0002-admin_console/plan.md)
**Status:** ⬜ Not started

## Scope

Implement the analytics-first dashboard (`/admin/dashboard`) and the
analytics overview page (`/admin/analytics/overview`) using real backend
data, replacing the placeholder `DashboardPage` shell from task 01.0. The
default route after login must be `/admin/dashboard`. This is part of the
milestone's MVP scope (see
[plan.md - Dependency graph](/docs/roadmap/0002-admin_console/plan.md)).

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Backend analytics overview](01-backend-analytics-overview.md) | ⬜ Not started | 02 |
| 02 | [Frontend dashboard UI](02-frontend-dashboard-ui.md) | ⬜ Not started | 03 |
| 03 | [Tests](03-tests.md) | ⬜ Not started | none |

## Key constraints

- Depends on task 01.0 (auth shell, `DashboardPage` placeholder, protected
  routing) and task 02.0 (pagination/time-range primitives,
  `PaginatedTable`/`DateRangePicker`/`GlobalFilterBar`, common error
  format) - do not re-implement either.
- Full fidelity for suspicious/bot/tampering/regeneration metrics depends on
  task 06.0's `SuspicionAnalyzer` populating risk data; until then those
  metrics may read as zero/empty against real data, which is expected and
  acceptable at this task's `⬜ Not started` stage (see
  [plan.md - Dependency graph](/docs/roadmap/0002-admin_console/plan.md)).
- Must query the immutable redirect-event stream or aggregate tables (see
  [05-data-model-and-storage.md §1](/docs/roadmap/0002-admin_console/05-data-model-and-storage.md)),
  never derive analytics from mutable URL pair rows.
- Must enforce `analytics:read` and mask sensitive values per the current
  user's permissions (
  [plan.md - Shared contracts C5, C6](/docs/roadmap/0002-admin_console/plan.md)).
- Empty database must render a correct zero-state, not an error.
