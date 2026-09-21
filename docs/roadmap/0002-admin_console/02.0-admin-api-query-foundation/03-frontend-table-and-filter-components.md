# 03 - Frontend Table and Filter Components

**Parent task:** 02.0 Admin API Query Foundation
**State:** ⬜ Not started
**Depends on:** 02
**Blocks:** 04

## Objective

Build the generic `PaginatedTable` and shared filter-bar/date-range UI every
later list and analytics page renders through, so table behavior (sorting,
pagination, loading/empty/error states, column visibility) is implemented
once.

## Files to add

```text
web/admin/src/components/tables/PaginatedTable.tsx
web/admin/src/components/tables/TableToolbar.tsx
web/admin/src/components/tables/ColumnVisibilityMenu.tsx
web/admin/src/components/analytics/DateRangePicker.tsx
web/admin/src/components/analytics/GlobalFilterBar.tsx
```

## Requirements

1. `PaginatedTable` is built on TanStack Table + TanStack Virtual (
   [00-decision-record.md §2](/docs/roadmap/0002-admin_console/00-decision-record.md))
   and must support, per
   [02-react-frontend-architecture.md §6](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md):

   ```text
   server-side pagination
   server-side sorting
   server-side filtering
   loading state
   empty state
   error state
   column visibility
   row click action
   compact density
   copy ID action
   ```

2. `TableToolbar` hosts search input (via `useDebouncedValue`), column
   visibility trigger, and row-density toggle.
3. `ColumnVisibilityMenu` persists per-table column visibility (e.g. via
   local component state or `localStorage`, project's choice) and is driven
   by `PaginatedTable`'s column definitions.
4. `DateRangePicker` and `GlobalFilterBar` implement the shared top-bar
   filter model from
   [01-product-and-ux-spec.md §3-4](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)
   (global time range + timezone selector, plus the shared filter fields),
   backed by `useDateRange`/`useSearchParamsState` from subtask 02.
5. Every empty/error state must use the `EmptyState`/`ErrorState` primitives
   from task 01.0 subtask 01, with page-appropriate copy (
   [01-product-and-ux-spec.md §9-10](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)).

## Constraints

- `PaginatedTable` must accept `PageResponse<T>` directly - no page should
  need to reshape API data before handing it to the table.
- Keep TanStack Table/Virtual usage encapsulated inside `PaginatedTable` -
  pages must not import table-library internals directly.

## Success criteria

- [ ] `PaginatedTable` renders mocked paginated data with working
      pagination controls.
- [ ] Sorting a column updates the `sort_by`/`sort_direction` query state.
- [ ] Loading, empty, and error states render distinct, correct UI.
- [ ] Column visibility toggling hides/shows columns without losing sort/
      filter state.
- [ ] `DateRangePicker` and `GlobalFilterBar` reflect their state in URL
      search params.
