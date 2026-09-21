# Task 05.0 - Visitor, Fingerprint, and IP Explorer

**Parent milestone:** [Milestone 0002 - Admin Console](/docs/roadmap/0002-admin_console/plan.md)
**Status:** ⬜ Not started

## Scope

Implement safe browsing and detail pages for visitor profiles,
fingerprints, and IP addresses - the identity graph. This task makes that
graph visible to admins without exposing raw fingerprint data
unnecessarily; every response must honor the masking rules in
[plan.md - Shared contract C5](/docs/roadmap/0002-admin_console/plan.md).

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Backend visitor profiles API](01-backend-visitor-profiles-api.md) | ⬜ Not started | 04 |
| 02 | [Backend fingerprints and IPs API](02-backend-fingerprints-and-ips-api.md) | ⬜ Not started | 04 |
| 03 | [Frontend privacy primitives](03-frontend-privacy-primitives.md) | ⬜ Not started | 04 |
| 04 | [Frontend explorer pages](04-frontend-explorer-pages.md) | ⬜ Not started | 05 |
| 05 | [Tests](05-tests.md) | ⬜ Not started | none |

## Key constraints

- Depends on task 01.0 (auth) and task 02.0 (pagination/`PaginatedTable`).
- Risk score, risk reasons, and smart-signal fields depend on task 06.0's
  `SuspicionAnalyzer` for real values; this task must still return the
  correct fields/shape (zero/empty risk data is acceptable pre-06.0, per
  [plan.md - Dependency graph](/docs/roadmap/0002-admin_console/plan.md)).
- Raw fingerprint components (canvas/audio/font/WebGL) are never shown by
  default, for any role.
- Backend decides full-vs-masked IP; the frontend renders exactly what it
  receives and never reconstructs a masked value client-side (
  [plan.md - Shared contract C5](/docs/roadmap/0002-admin_console/plan.md)).
