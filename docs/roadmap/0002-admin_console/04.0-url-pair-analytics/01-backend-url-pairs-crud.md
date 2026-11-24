# 01 - Backend URL Pairs CRUD

**Parent task:** 04.0 URL Pair List and URL Pair Analytics
**State:** ⬜ Not started
**Depends on:** none (builds on task 02.0's query primitives)
**Blocks:** 03

## Objective

Implement the admin URL-pair listing, detail, and update/disable endpoints
backing `/admin/url-pairs` and the admin-only disable action.

## Files to add

```text
src/admin/controllers/UrlPairsAdminController.hpp
src/admin/controllers/UrlPairsAdminController.cpp
src/admin/dto/UrlPairAdminDto.hpp
src/admin/dto/UrlPairAdminDto.cpp
```

No `UrlPairsAdminRepository` class: the controller calls
`registry.execute` through `AdminCommandBridge` (task 02.0 subtask 01)
for all list, get, update, and disable operations.

## API contract

```http
GET    /admin/api/v1/url-pairs
GET    /admin/api/v1/url-pairs/{url_pair_id}
PATCH  /admin/api/v1/url-pairs/{url_pair_id}
```

Write operations (`PATCH`, including status/disable changes) require
`url_pairs:write` (
04-admin-api-contract.md §5 (`docs/roadmap/0002-admin_console/04-admin-api-contract.md`, planned, not yet written)).

Registry command mapping:

| Admin endpoint | Registry command |
|---|---|
| `GET /admin/api/v1/url-pairs` | `link.list` |
| `GET /admin/api/v1/url-pairs/{url_pair_id}` | `link.get` with `id` arg (D1 confirmed: `id` accepted alongside `slug`) |
| `PATCH /admin/api/v1/url-pairs/{url_pair_id}` | `link.update`, `link.disable`, or `link.enable` with `id` arg (D1 confirmed) |

## Requirements

1. `GET /url-pairs` is paginated/sorted/filtered via task 02.0's
   `PageRequest`/`AdminFilterParser`, and searchable by short code and
   target domain.
2. `UrlPairAdminDto` fields cover the required list columns (
   01-product-and-ux-spec.md §8.1 (`docs/roadmap/0002-admin_console/01-product-and-ux-spec.md`, planned, not yet written)):
   `Short Code, Target URL, Created At, Updated At, Status, Redirect Count,
   Unique Visitors, Last Redirect`.
3. `PATCH /url-pairs/{url_pair_id}` supports at least a disable action
   (status transition); reject the request with `not_authorized` for
   callers lacking `url_pairs:write`.
4. Every successful disable writes an audit log event (
   [plan.md - Shared contract C7](/docs/roadmap/0002-admin_console/plan.md));
   route this through the same interface task 01.0 subtask 02 stubbed for
   login/logout events so task 07.0's `AuditLogger` can slot in cleanly.
5. `Redirect Count`/`Unique Visitors`/`Last Redirect` are sourced from
   `link.stats` (via `AdminCommandBridge`), which queries the immutable
   redirect-event stream or aggregate tables (
   05-data-model-and-storage.md §1 (`docs/roadmap/0002-admin_console/05-data-model-and-storage.md`, planned, not yet written)),
   not by mutating the URL pair row.

## Constraints

- Do not implement delete in this subtask unless the underlying storage
  backend already supports safe deletion - per
  01-product-and-ux-spec.md §8.1 (`docs/roadmap/0002-admin_console/01-product-and-ux-spec.md`, planned, not yet written)
  delete is conditional ("only if backend supports safe deletion"); disable
  is the required MVP write action.

## Success criteria

- [ ] `GET /url-pairs` returns paginated, sortable, filterable results
      searchable by short code and target domain.
- [ ] `GET /url-pairs/{id}` returns a single URL pair's admin DTO.
- [ ] `PATCH /url-pairs/{id}` disables a URL pair for a caller with
      `url_pairs:write` and is rejected with `not_authorized` otherwise.
- [ ] Disable action is audit-logged.
