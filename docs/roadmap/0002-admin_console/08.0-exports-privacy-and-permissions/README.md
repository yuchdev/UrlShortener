# Task 08.0 - Exports, Privacy, and Permissions

**Parent milestone:** [Milestone 0002 - Admin Console](/docs/roadmap/0002-admin_console/plan.md)
**Status:** ⬜ Not started

## Scope

Implement controlled data exports and enforce privacy/masking policies
consistently across every admin API built in tasks 03.0-07.0. This task
also delivers the real `MaskingService`/`PrivacyPolicy` that earlier tasks'
narrow masking hooks (IP display in task 05.0, sensitive value handling
generally) were built to accept.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Backend privacy and masking](01-backend-privacy-and-masking.md) | ⬜ Not started | 02 |
| 02 | [Backend export service](02-backend-export-service.md) | ⬜ Not started | 03 |
| 03 | [Frontend export and privacy UI](03-frontend-export-and-privacy-ui.md) | ⬜ Not started | 04 |
| 04 | [Tests](04-tests.md) | ⬜ Not started | none |

## Key constraints

- Depends on task 01.0 (auth), task 02.0 (pagination), and reaches back into
  task 05.0's IP-masking hook and task 03.0/04.0's analytics DTOs to route
  their masking decisions through the real `MaskingService` built here.
- Exports must apply the exact same masking rules as the live APIs - never a
  separately-maintained export-only masking path (
  [plan.md - Shared contract C5](/docs/roadmap/0002-admin_console/plan.md)).
- Every export is audit-logged via task 07.0's `AuditLogger` (
  [plan.md - Shared contract C7](/docs/roadmap/0002-admin_console/plan.md)).
- Read-only users may export aggregate analytics only (if
  `exports:create_aggregate` is granted); raw redirect-event export is
  admin-only and additionally policy-gated by `exports:create_raw`.
