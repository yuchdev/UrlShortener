# 04 - Tests

**Parent task:** 02.0 Admin API Query Foundation
**State:** ⬜ Not started
**Depends on:** 01, 03
**Blocks:** none

## Objective

Cover the query primitives (backend) and hooks/table component (frontend)
built in subtasks 01-03, and lock in the common error-format contract every
later admin endpoint relies on.

## Files to add

Frontend:

```text
web/admin/src/hooks/usePagination.test.ts
web/admin/src/hooks/useSearchParamsState.test.ts
web/admin/src/components/tables/PaginatedTable.test.tsx
```

Backend:

```text
tests/unit/admin/PageRequestTest.cpp
tests/unit/admin/TimeRangeTest.cpp
tests/unit/admin/AdminFilterParserTest.cpp
tests/integration/admin/AdminApiErrorFormatTest.cpp
```

## Requirements

1. `PageRequestTest` covers default values, valid overrides, invalid
   `page`/`page_size`, and the `page_size` cap at 500.
2. `TimeRangeTest` covers valid ranges for each granularity, timezone
   handling, and rejection of impossible ranges.
3. `AdminFilterParserTest` covers each filter field from the shared
   vocabulary, including absent/malformed values.
4. `AdminApiErrorFormatTest` is an integration test asserting that at least
   one real endpoint (the auth endpoints from task 01.0 are sufficient,
   since this task predates any list endpoint) returns the exact
   `{error:{code,message,request_id}}` shape for a `validation_error` and a
   `not_authorized` case.
5. `usePagination.test.ts` / `useSearchParamsState.test.ts` cover state
   round-tripping through simulated URL search params.
6. `PaginatedTable.test.tsx` covers rendering mocked `PageResponse` data,
   pagination control clicks, and empty/error state rendering.

## Constraints

- These tests must pass before any task 03.0-08.0 subtask builds on this
  foundation - treat this subtask as a hard gate, not a nice-to-have.

## Success criteria

- [ ] All backend unit tests above pass under CTest.
- [ ] `AdminApiErrorFormatTest` passes as an integration test against the
      running admin router.
- [ ] All frontend unit tests above pass under `npm run test`.
- [ ] Query params are demonstrably reflected in URL search params in at
      least one hook test.
- [ ] Every admin API error path exercised by these tests uses the common
      error format.
