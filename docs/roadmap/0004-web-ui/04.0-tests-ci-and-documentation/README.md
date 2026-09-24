# Task 04.0 - Tests, CI, and Documentation

**Parent milestone:** [Milestone 0004 - Public Web UI](/docs/roadmap/0004-web-ui/plan.md)
**Status:** ⬜ Not started

## Scope

Harden and finish the milestone: consolidate the per-task component-test setup
into shared frontend test infrastructure, add a Playwright e2e suite that
exercises the happy path **and every fault mode** enumerated in task 02.0
(not just a smoke test), wire CI, and write `docs/web-app/`.

This task does not add product features; it makes the existing ones provably
correct and documented, and folds the ad-hoc test setup from tasks 01.0-03.0
into one place.

## Subtasks

| # | Document | Status | Depends on | Blocks |
|---|----------|--------|------------|--------|
| 01 | [Frontend test infrastructure](01-frontend-test-infrastructure.md) | ⬜ Not started | 01.0-03.0 | 02 |
| 02 | [Playwright e2e suite](02-playwright-e2e-suite.md) | ⬜ Not started | 01 | none |
| 03 | [Documentation](03-documentation.md) | ⬜ Not started | 01.0-03.0 | none |

## Key constraints

- The e2e suite must cover the happy path **and** each fault mode from task
  02.0's fault-state model — a smoke test alone does not satisfy this
  milestone (see [plan.md - Global acceptance criteria](/docs/roadmap/0004-web-ui/plan.md)).
- Test infra consolidation must dedupe, not duplicate, the MSW/Vitest setup
  introduced locally in tasks 01.0-03.0.
- CI runs frontend lint / typecheck / unit / build and the e2e suite.
- Docs must state honestly that `/app` is public/unauthenticated and how it
  relates to 0002's login-gated `/admin` and the existing `/api/v1/links` API.
