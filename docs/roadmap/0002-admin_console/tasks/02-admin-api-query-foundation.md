# Task 02 — Admin API Query Foundation

## Goal

Create reusable backend and frontend infrastructure for paginated admin queries, filters, sorting, and typed DTOs.

This task prepares all later analytics/list pages.

## Required frontend files

Create:

```text
web/admin/src/model/pagination.ts
web/admin/src/model/filters.ts
web/admin/src/model/analytics.ts
web/admin/src/api/schemas.ts
web/admin/src/hooks/usePagination.ts
web/admin/src/hooks/useSearchParamsState.ts
web/admin/src/hooks/useDateRange.ts
web/admin/src/hooks/useDebouncedValue.ts
web/admin/src/components/tables/PaginatedTable.tsx
web/admin/src/components/tables/TableToolbar.tsx
web/admin/src/components/tables/ColumnVisibilityMenu.tsx
web/admin/src/components/analytics/DateRangePicker.tsx
web/admin/src/components/analytics/GlobalFilterBar.tsx
```

## Required backend files

Create:

```text
src/admin/query/PageRequest.h
src/admin/query/PageRequest.cpp
src/admin/query/PageResponse.h
src/admin/query/PageResponse.cpp
src/admin/query/SortSpec.h
src/admin/query/SortSpec.cpp
src/admin/query/TimeRange.h
src/admin/query/TimeRange.cpp
src/admin/query/AdminFilterParser.h
src/admin/query/AdminFilterParser.cpp
src/admin/query/AdminQueryError.h
src/admin/query/AdminQueryError.cpp
src/admin/dto/PaginationDto.h
src/admin/dto/PaginationDto.cpp
```

## Requirements

### Pagination

All list endpoints must support:

```text
page
page_size
sort_by
sort_direction
```

Defaults:

```text
page = 1
page_size = 50
maximum page_size = 500
sort_direction = desc where appropriate
```

### Time range

All analytics endpoints must support:

```text
from
to
granularity
timezone
```

Accepted granularities:

```text
minute
hour
day
```

Backend must validate time ranges and reject impossible ranges.

### Error format

All admin API errors must follow:

```json
{
  "error": {
    "code": "validation_error",
    "message": "Invalid page_size",
    "request_id": "req_..."
  }
}
```

## Tests to add

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

## Exit criteria

- Pagination parser handles valid and invalid input.
- Page size is capped.
- Time range parser handles timezone and granularity.
- Frontend table component can render mocked paginated data.
- Query params are reflected in URL search params.
- All admin API errors use the common error format.
