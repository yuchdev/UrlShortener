# Task 09.0 - Tests, CI, and Documentation

**Parent milestone:** [Milestone 0002 - Admin Console](/docs/roadmap/0002-admin_console/plan.md)
**Status:** ⬜ Not started

## Scope

Add the cross-cutting test infrastructure, CI wiring, and documentation that
ties together everything built in tasks 01.0-08.0: e2e smoke coverage across
the full login -> dashboard -> permissions -> URL-pairs flow, a CI workflow
running both frontend and backend suites, and `docs/admin-console/` covering
setup, architecture, API, fingerprinting, and security/privacy.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Frontend test infrastructure](01-frontend-test-infrastructure.md) | ⬜ Not started | none |
| 02 | [Backend test groups and CI](02-backend-test-groups-and-ci.md) | ⬜ Not started | none |
| 03 | [Documentation](03-documentation.md) | ⬜ Not started | none |

## Key constraints

- Depends on every prior task (01.0-08.0) being at least scaffolded, since
  this task's e2e specs and documentation exercise the full flow end to
  end; in practice this task is finished last, after 01.0-08.0 land.
- Every test group referenced here (`tests/unit/admin/`,
  `tests/integration/admin/`, `tests/unit/fingerprint/`,
  `tests/integration/tracking/`, `tests/fixtures/fingerprints/`) already
  exists from tasks 01.0-08.0 - this task ensures they are wired into CI,
  not that they are created from scratch.
- `docs/admin-console/` documentation must explain first-admin creation,
  fingerprint provider configuration, and privacy/export policy - it is not
  optional polish, it is a named exit criterion of the source task.
