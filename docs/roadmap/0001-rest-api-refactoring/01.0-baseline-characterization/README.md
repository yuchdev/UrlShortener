# Task 01.0 - Baseline characterization

**Parent milestone:** [Milestone 0001 - REST API Refactoring](/docs/roadmap/0001-rest-api-refactoring/plan.md)
**Status:** ✅ Complete

## Scope

Before any production dispatch code moves, lock down the current behavior of
every HTTP endpoint with behavior-preserving tests. This covers the full
endpoint matrix, wrong-method/validation-error responses, and redirect and
fallback semantics (including root-redirect fallthrough to
`handleApplicationRequest()`). No production files are touched by this task -
tests only, so that every later migration stage can be reviewed as a
mechanical, behavior-preserving refactor against this baseline.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Characterize the REST endpoint matrix](01-endpoint-matrix-characterization.md) | ✅ Complete | 02, 03 |
| 02 | [Characterize method and error behavior](02-error-and-method-characterization.md) | ✅ Complete | 03 |
| 03 | [Characterize redirect and fallback behavior](03-redirect-and-fallback-characterization.md) | ✅ Complete | none |

## Key constraints

- No production files are changed by any subtask in this task - tests only.
- Current behavior is asserted even where it is not ideal REST semantics
  (e.g. `400 invalid_method` instead of `405 Method Not Allowed`); this task
  does not change response semantics.
- Every new test file is registered under the `unit` CTest label.
