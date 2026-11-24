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
src/admin/query/PageRequest.hpp
src/admin/query/PageRequest.cpp
src/admin/query/PageResponse.hpp
src/admin/query/PageResponse.cpp
src/admin/query/SortSpec.hpp
src/admin/query/SortSpec.cpp
src/admin/query/TimeRange.hpp
src/admin/query/TimeRange.cpp
src/admin/query/AdminFilterParser.hpp
src/admin/query/AdminFilterParser.cpp
src/admin/query/AdminQueryError.hpp
src/admin/query/AdminQueryError.cpp
src/admin/dto/PaginationDto.hpp
src/admin/dto/PaginationDto.cpp
src/admin/query/AdminCommandBridge.hpp
src/admin/query/AdminCommandBridge.cpp
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

### Registry projection

`AdminCommandBridge` is the single point that translates between the admin
API's pagination vocabulary and the command registry. It provides:

```cpp
CommandRequest toCommand(const PageRequest&, const AdminFilter&,
                         std::string_view command);
AdminQueryError fromCommandError(std::string_view code);
PageResponse<T> fromListResult(/* ... */);
```

Pagination mapping: `offset = (page - 1) * page_size`; `limit = page_size`
(capped at 500, same as `link.list`); `sort_by` -> `sort` (allowed:
`created_at`, `slug`, `updated_at`; other values -> `validation_error`);
`sort_direction` -> `order`; `PageResponse.total` = command result `total`.

Filter mapping: `status`, `tag`, `campaign`, `q` (from `short_code`/
`target_domain`) are forwarded to `link.list` args. Analytics-side filters
(`visitor_profile_id`, `fingerprint_id`, `ip_id`, `referrer_domain`,
`risk_level`, `bot_status`) are not passed through the bridge; they remain
0002-owned query parameters.

Error mapping: command codes translate to admin API codes per
[plan.md - Shared contract C4](/docs/roadmap/0002-admin_console/plan.md).

## Constraints

- `PageRequest`/`PageResponse` must be usable by every controller added in
  tasks 03.0-08.0 without per-endpoint duplication.
- `AdminFilterParser` should parse the shared filter vocabulary from
  01-product-and-ux-spec.md §4 (`docs/roadmap/0002-admin_console/01-product-and-ux-spec.md`, planned, not yet written)
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
- [ ] `AdminCommandBridge` maps every command error code in the catalog to
      an admin API code per plan.md C4.
