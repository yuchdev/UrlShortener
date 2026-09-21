# Task 04.0 - URL Pair List and URL Pair Analytics

**Parent milestone:** [Milestone 0002 - Admin Console](/docs/roadmap/0002-admin_console/plan.md)
**Status:** ⬜ Not started

## Scope

Implement paginated URL pair browsing (`/admin/url-pairs`) and per-link
analytics detail pages (`/admin/analytics/links`, `/admin/analytics/links/
{url_pair_id}`), including the admin-only "disable URL pair" write action.
This is part of the milestone's MVP scope alongside task 03.0 (see
[plan.md - Dependency graph](/docs/roadmap/0002-admin_console/plan.md)).

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Backend URL pairs CRUD](01-backend-url-pairs-crud.md) | ⬜ Not started | 03 |
| 02 | [Backend link analytics](02-backend-link-analytics.md) | ⬜ Not started | 03 |
| 03 | [Frontend URL pairs and link analytics UI](03-frontend-url-pairs-and-link-analytics-ui.md) | ⬜ Not started | 04 |
| 04 | [Tests](04-tests.md) | ⬜ Not started | none |

## Key constraints

- Depends on task 01.0 (auth), task 02.0 (`PaginatedTable`, pagination/
  filter/error-format primitives), and reuses `MetricCard`/`TimeSeriesChart`
  patterns from task 03.0.
- The "disable URL pair" action is admin-only (`url_pairs:write`), requires
  a confirmation dialog and a CSRF token, and must be audit-logged (
  [plan.md - Shared contracts C2, C7](/docs/roadmap/0002-admin_console/plan.md));
  read-only users must be rejected by the backend even if the frontend
  control were somehow reachable.
- Link detail analytics (referrers, visitor profiles, suspicious events) may
  read as sparse until tasks 05.0/06.0 land, but the query paths must be
  correct against real data, not stubbed.
