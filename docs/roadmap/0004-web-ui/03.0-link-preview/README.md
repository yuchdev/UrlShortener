# Task 03.0 - Link Preview

**Parent milestone:** [Milestone 0004 - Public Web UI](/docs/roadmap/0004-web-ui/plan.md)
**Status:** ⬜ Not started

## Scope

Add a minimal public preview page at `/app/preview/{slug}` that shows, for an
existing short link, its target URL, status, and whether it is currently
active — over the **already-public** preview endpoint. No new backend endpoint
is needed: `GET /api/v1/links/{slug}/preview` (`handlePreviewLink` in
`src/http/handlers/link_handlers.cpp`) exists and requires no auth today (see
[plan.md SC1](/docs/roadmap/0004-web-ui/plan.md)).

This is a small, read-only feature. It reuses the shell, API client, Zod
schemas (the preview schema is defined in
[01.0 subtask 03](/docs/roadmap/0004-web-ui/01.0-web-app-shell-static-hosting-shorten-flow/03-api-client-and-schemas.md)),
and the fault-state model from task 02.0.

## Subtasks

| # | Document | Status | Depends on | Blocks |
|---|----------|--------|------------|--------|
| 01 | [Frontend preview page](01-frontend-preview-page.md) | ⬜ Not started | 01.0, 02.0/02 | 02 |
| 02 | [Tests](02-tests.md) | ⬜ Not started | 01 | none |

## Key constraints

- No new backend endpoint — the existing public preview endpoint is used
  as-is.
- Read-only: the page shows target/status/active and never edits, disables,
  or deletes (see [plan.md SC6](/docs/roadmap/0004-web-ui/plan.md)).
- Not-found / expired / disabled are explicit, honest states, reusing the
  fault-state model where applicable.
- The rendered target URL is untrusted content — display it safely (no
  auto-navigation, no execution; render as text/inert link with appropriate
  `rel`).
