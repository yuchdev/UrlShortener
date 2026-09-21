# Task 02.0 - Admin API Query Foundation

**Parent milestone:** [Milestone 0002 - Admin Console](/docs/roadmap/0002-admin_console/plan.md)
**Status:** ⬜ Not started

## Scope

Build the reusable backend and frontend infrastructure for paginated admin
queries, filters, sorting, and typed DTOs that every later analytics/list
page depends on. Nothing in this task is user-visible on its own - it exists
so tasks 03.0 through 08.0 do not each reinvent pagination, time-range
parsing, or the table/filter UI.

This is the pagination/filter/error-format contract described in
[plan.md - Shared contracts C3 and C4](/docs/roadmap/0002-admin_console/plan.md);
those contracts are authoritative and this task is where they get built.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Backend query primitives](01-backend-query-primitives.md) | ⬜ Not started | 04 |
| 02 | [Frontend query models and hooks](02-frontend-query-models-and-hooks.md) | ⬜ Not started | 03 |
| 03 | [Frontend table and filter components](03-frontend-table-and-filter-components.md) | ⬜ Not started | 04 |
| 04 | [Tests](04-tests.md) | ⬜ Not started | none |

## Key constraints

- Depends on task 01.0 for the authenticated `apiClient` and CSRF wiring
  that the frontend query hooks build requests through.
- Blocks tasks 03.0 through 08.0: every later list/analytics page uses
  `PageRequest`/`PageResponse`, `PaginatedTable`, and the shared error
  format built here.
- Pagination defaults: `page=1`, `page_size=50`, `page_size` capped at
  `500`.
- Time-range endpoints accept `from`, `to`, `granularity`
  (`minute|hour|day`), `timezone`, and must reject impossible ranges
  (e.g. `from > to`, unsupported granularity).
- All admin API errors use the common `{error:{code,message,request_id}}`
  format from
  [plan.md - Shared contract C4](/docs/roadmap/0002-admin_console/plan.md).
- Query params must be reflected in URL search params so filtered views are
  shareable internally.
