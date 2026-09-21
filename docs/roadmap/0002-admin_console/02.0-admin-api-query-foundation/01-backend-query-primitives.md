# 01 - Backend Query Primitives

**Parent task:** 02.0 Admin API Query Foundation
**State:** ⬜ Not started
**Depends on:** none
**Blocks:** 04

## Objective

Implement the shared C++ types every admin list/analytics endpoint parses
its query string into and serializes its response with: page request/
response, sort spec, time range, filter parsing, and the common error type.

## Files to add

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

Defaults: `page=1`, `page_size=50`, maximum `page_size=500`,
`sort_direction=desc` where appropriate.

### Time range

All analytics endpoints must support `from`, `to`, `granularity`,
`timezone`. Accepted granularities: `minute`, `hour`, `day`. `TimeRange`
must validate the range and reject impossible ranges (`from` after `to`,
unsupported granularity, range too large for `minute` granularity, etc.)
with a `validation_error`.

### Error format

All admin API errors use:

```json
{ "error": { "code": "validation_error", "message": "Invalid page_size", "request_id": "req_..." } }
```

`AdminQueryError` is the shared type controllers throw/return to produce
this shape consistently - see
[plan.md - Shared contract C4](/docs/roadmap/0002-admin_console/plan.md).

## Constraints

- `PageRequest`/`PageResponse` must be usable by every controller added in
  tasks 03.0-08.0 without per-endpoint duplication.
- `AdminFilterParser` should parse the shared filter vocabulary from
  [01-product-and-ux-spec.md §4](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)
  (`short_code`, `url_pair_id`, `target_domain`, `visitor_profile_id`,
  `fingerprint_id`, `ip_id`, `referrer_domain`, `risk_level`, `bot_status`,
  `status`) generically rather than one parser per endpoint.

## Success criteria

- [ ] `PageRequest` parses valid and invalid `page`/`page_size`/`sort_by`/
      `sort_direction` combinations.
- [ ] `page_size` above 500 is capped, not rejected.
- [ ] `TimeRange` parses valid combinations and rejects impossible ranges
      with `validation_error`.
- [ ] `AdminFilterParser` parses the full shared filter vocabulary.
- [ ] `AdminQueryError` serializes to the exact common error JSON shape.
