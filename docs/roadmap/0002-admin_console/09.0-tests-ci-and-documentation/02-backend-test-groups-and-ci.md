# 02 - Backend Test Groups and CI

**Parent task:** 09.0 Tests, CI, and Documentation
**State:** ⬜ Not started
**Depends on:** none
**Blocks:** none

## Objective

Confirm the backend test groups from tasks 01.0-08.0 are complete and wire a
CI workflow running frontend lint/typecheck/unit/build/e2e-smoke and backend
build/unit/integration jobs.

## Test groups to verify exist

```text
tests/unit/admin/
tests/integration/admin/
tests/unit/fingerprint/
tests/integration/tracking/
tests/fixtures/fingerprints/
```

These are created across tasks 01.0-08.0's own test subtasks - this subtask
audits that every file each task's spec named actually landed in the right
group, not that the groups are created fresh.

## CI file to add/extend

```text
.github/workflows/admin-console.yml
```

## CI jobs required

```text
frontend lint
frontend typecheck
frontend unit tests
frontend build
frontend e2e smoke tests
backend build
backend unit tests
backend integration tests
```

## Requirements

1. `admin-console.yml` triggers on changes under `web/admin/`,
   `web/public-tracker/`, `src/admin/`, `src/fingerprint/`, `src/tracking/`,
   and `docs/roadmap/0002-admin_console/` (or the project's existing
   path-filter convention if one already exists - check
   `.github/workflows/` for an established pattern before inventing a new
   one).
2. Frontend jobs run in `web/admin/` using the package scripts from subtask
   01 (`lint`, `typecheck`, `test`, `build`, `e2e`).
3. Backend jobs build and run the project's existing CTest-based suite,
   scoped (or not) to the admin/fingerprint/tracking targets depending on
   how the project's existing CI already scopes CTest runs.
4. E2E job depends on both a built frontend and a running backend (or a
   suitably mocked backend) - document whichever approach is chosen in this
   file's comments so task 09.0 subtask 03's docs can reference it
   accurately.
5. Cross-check every backend test file named across tasks 01.0-08.0's test
   subtasks is present under one of the five test groups above; file a
   follow-up note in `status.md` if any are missing once tasks are actually
   implemented (this subtask's own job at the design stage is to make sure
   the *plan* accounts for all of them, which it does per the per-task
   subtask specs).

## Constraints

- Do not duplicate or replace the project's existing top-level CI workflow
  for the C++ backend - extend or add alongside it, following whatever
  convention `.github/workflows/` already uses.

## Success criteria

- [ ] `.github/workflows/admin-console.yml` exists with all eight required
      job categories.
- [ ] CI runs frontend and backend tests on every relevant PR.
- [ ] React app builds successfully in CI.
- [ ] Admin API integration tests run in CI.
- [ ] E2E smoke test job covers login and dashboard.
