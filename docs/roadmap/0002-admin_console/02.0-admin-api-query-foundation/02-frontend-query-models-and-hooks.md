# 02 - Frontend Query Models and Hooks

**Parent task:** 02.0 Admin API Query Foundation
**State:** ⬜ Not started
**Depends on:** none (parallel with subtask 01; the two integrate against
each other only through the shared DTO shapes, which are fixed by
[04-admin-api-contract.md](/docs/roadmap/0002-admin_console/04-admin-api-contract.md))
**Blocks:** 03

## Objective

Implement the frontend-side pagination/filter/date-range models and the
hooks that keep query state (page, filters, date range, debounced search
text) synchronized with URL search params.

## Files to add

```text
web/admin/src/model/pagination.ts
web/admin/src/model/filters.ts
web/admin/src/model/analytics.ts
web/admin/src/api/schemas.ts
web/admin/src/hooks/usePagination.ts
web/admin/src/hooks/useSearchParamsState.ts
web/admin/src/hooks/useDateRange.ts
web/admin/src/hooks/useDebouncedValue.ts
```

## Requirements

1. `model/pagination.ts` defines the `PageRequest`/`PageResponse<T>` types
   matching the backend shape exactly (
   [02-react-frontend-architecture.md §6](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md)):

   ```ts
   export interface PageRequest {
     page: number;
     page_size: number;
     sort_by?: string;
     sort_direction?: 'asc' | 'desc';
   }

   export interface PageResponse<T> {
     items: T[];
     pagination: { page: number; page_size: number; total_items: number; total_pages: number };
   }
   ```

2. `model/filters.ts` defines the shared filter vocabulary from
   [01-product-and-ux-spec.md §4](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md).
3. `api/schemas.ts` provides Zod schemas for `PageResponse`, the error
   envelope, and the shared filter model, used to validate key responses in
   development/test mode (
   [02-react-frontend-architecture.md §3](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md)).
4. `useSearchParamsState` is a generic hook syncing arbitrary typed state to
   URL search params - `usePagination` and `useDateRange` are built on top
   of it, not independent implementations.
5. `useDebouncedValue` debounces free-text filter input (e.g. short-code
   search) before it reaches a query key.
6. React Query keys following
   [02-react-frontend-architecture.md §5](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md)
   conventions (hierarchical, stable, filters/pagination included in the
   key) must be demonstrated in at least one example hook here so later
   tasks copy a correct pattern.

## Constraints

- Do not duplicate the pagination/filter shape definitions per feature
  module - every later `*Api.ts` module imports from here.
- Keep Zod validation dev/test-only (do not pay parsing cost in production
  builds) per the architecture doc.

## Success criteria

- [ ] `PageRequest`/`PageResponse<T>` types match the backend DTO exactly.
- [ ] `useSearchParamsState` round-trips typed state through URL search
      params.
- [ ] `usePagination` and `useDateRange` persist page/filter state across a
      simulated navigation in a unit test.
- [ ] `useDebouncedValue` delays updates by the configured interval.
- [ ] Zod schemas in `schemas.ts` validate a known-good and reject a
      known-bad `PageResponse` fixture.
