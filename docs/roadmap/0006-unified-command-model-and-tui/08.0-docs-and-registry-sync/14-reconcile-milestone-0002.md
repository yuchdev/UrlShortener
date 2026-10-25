# 14 - Reconcile Milestone 0002

**Parent task:** 08.0 Docs and registry sync
**State:** ⬜ Not started
**Depends on:** plan.md "Impact on other milestones" (can run before implementation; re-verify after Tasks 03.0/04.0 settle)
**Blocks:** 16
**Estimated size:** M
**Suggested agent:** docs-updater (edits), app-architect (reviews the projection design)

## Objective

Apply plan.md's decision (decision-log Q15) that the admin API
`/admin/api/v1/*` is a **projection** of the command registry for link/pair
browsing and mutation, by making the exact edits to Milestone 0002's documents.
Only link/pair features are affected; cookie/CSRF auth (C2), analytics,
fingerprint, console-user and audit endpoints keep their own design.

## Dependencies

- 0006 plan.md "Impact on other milestones", catalog sections 3.1-3.2, 3.5.
- Files read for this spec: `docs/roadmap/0002-admin_console/plan.md` (already carries a "Note (added with Milestone 0006)" block after the decision-record paragraph; Shared contracts C3 pagination and C4 error format; Tasks table; dependency graph), `02.0-admin-api-query-foundation/README.md` + `01-backend-query-primitives.md`, `04.0-url-pair-analytics/README.md` + `01-backend-url-pairs-crud.md` + `02-backend-link-analytics.md`, `07.0-console-users-and-audit-log/README.md`, `status.md`.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `docs/roadmap/0002-admin_console/plan.md` | modify | note, C3, C4, tasks table cells, dependency graph |
| `docs/roadmap/0002-admin_console/02.0-admin-api-query-foundation/README.md` | modify | scope + key constraints |
| `docs/roadmap/0002-admin_console/02.0-admin-api-query-foundation/01-backend-query-primitives.md` | modify | files list, requirements, new bridge |
| `docs/roadmap/0002-admin_console/04.0-url-pair-analytics/README.md` | modify | key constraints |
| `docs/roadmap/0002-admin_console/04.0-url-pair-analytics/01-backend-url-pairs-crud.md` | modify | files list and contract |
| `docs/roadmap/0002-admin_console/04.0-url-pair-analytics/02-backend-link-analytics.md` | modify | `link.stats` reuse note |
| `docs/roadmap/0002-admin_console/07.0-console-users-and-audit-log/README.md` | modify | registry integration constraint |
| `docs/roadmap/0002-admin_console/status.md` | modify | prerequisite line |

## Classes and functions (exact edits)

**plan.md**

1. Replace the existing `> **Note (added with Milestone 0006)**` block with: "Prerequisite: Milestone 0006 must be complete. `/admin/api/v1/url-pairs*` is a projection of registry commands (`link.list`, `link.get`, `link.update`, `link.enable`, `link.disable`, `link.stats`; `db.rows` for generic table views). Auth/session (C2), analytics, fingerprint, console-user and audit endpoints are not commands." Keep the link to 0006 plan.
2. **C3 Pagination:** append "Mapping for registry-backed lists: `offset = (page - 1) * page_size`, `limit = page_size` (`1..500`, same cap as `link.list`), `sort_by` -> `sort` (allowed: `created_at`, `slug`, `updated_at`; other values -> `validation_error`), `sort_direction` -> `order`, `PageResponse.total` = command `total`. Built once in `AdminCommandBridge` (task 02.0 subtask 01)."
3. **C4 Error format:** append the mapping table: `not_found`->`not_found`; `invalid_field`, `invalid_url`, `invalid_slug`, `reserved_slug`, `invalid_envelope`->`validation_error`; `slug_conflict`->`conflict`; `not_authorized`->`not_authorized`; `busy`->`rate_limited`; `job_cancelled`->(job state, not an error response); `unsupported`, `storage_failure`, `internal`, `unknown_command`->`internal_error`. Note `request_id` remains an admin concept (not part of the command envelope).
4. **Tasks table:** row 02.0 Output append "(link/pair lists reuse registry commands via `AdminCommandBridge`)"; row 04.0 Output append "(backed by `link.*` commands)"; row 07.0 Output append "(adds `auth.*`/`user.*` descriptors, permission `manage_users`)".
5. **Dependency graph:** add above `01.0` a line `0006 Unified Command Model (prerequisite for 02.0/04.0)`.

**02.0 README:** Scope paragraph: add "Link and URL-pair list endpoints are projections of registry commands; this task builds the projection layer (`AdminCommandBridge`) plus the primitives needed by analytics endpoints." Key constraints: add bullet "Do not re-implement list/filter/sort for links; map to `link.list` (Milestone 0006 catalog section 3.2)".

**02.0 / 01-backend-query-primitives.md:** `Files to add`: keep `PageRequest`, `PageResponse`, `SortSpec`, `TimeRange`, `AdminFilterParser`, `AdminQueryError`, `PaginationDto`; **add** `src/admin/query/AdminCommandBridge.hpp/.cpp` (`CommandRequest toCommand(const PageRequest&, const AdminFilter&, std::string_view command)`, `AdminQueryError fromCommandError(std::string_view code)`, `PageResponse<T> fromListResult(...)`). Under `Requirements`: add a "Registry projection" subsection with the C3/C4 mappings; note `AdminFilterParser` keeps the full vocabulary but only `status`, `tag`, `campaign`, `q` (from `short_code`/`target_domain`) map to `link.list` args - unsupported filters (`visitor_profile_id`, `fingerprint_id`, `ip_id`, `referrer_domain`, `risk_level`, `bot_status`) are analytics-side and stay out of the bridge. Success criteria: add "`AdminCommandBridge` maps every command error code in the catalog".

**04.0 README:** Key constraints: add "URL-pair listing, detail and disable are backed by registry commands (Milestone 0006); no `UrlPairsAdminRepository` direct storage access for links." **04.0 / 01-backend-url-pairs-crud.md:** remove `UrlPairsAdminRepository.hpp/.cpp` from `Files to add` (replace with a note that the controller calls `registry.execute` through the bridge); in the API contract add a table: `GET /admin/api/v1/url-pairs` -> `link.list`; `GET .../{url_pair_id}` -> `link.get {id}`; `PATCH .../{url_pair_id}` -> `link.update`/`link.disable`/`link.enable` (see design problem below). **04.0 / 02-backend-link-analytics.md:** add "basic per-link time series reuse `link.stats`; referrer/visitor/suspicion aggregates are 0002-owned queries."

**07.0 README:** Key constraints add: "Console-user and audit operations are exposed as registry descriptors (`auth.*`, `user.*`, permission `manage_users`) per Milestone 0006 plan; CLAUDE.md rule stands: auth broker local-only, no `0.0.0.0`; no argument values in audit lines."

**status.md:** under Current gate status add "Prerequisite: Milestone 0006 (command model)."

## Implementation steps

1. Re-read each file just before editing (they may have moved on); 2. apply edits; 3. `/doc-xref` on the renamed/removed file names (`UrlPairsAdminRepository`); 4. `/link-check docs/roadmap/0002-admin_console`.

## Commands

```bash
git grep -n "UrlPairsAdminRepository\|PageRequest" -- docs
```

## Tests

None executable; link check clean.

## Constraints

- Only the listed edits; 0002's analytics/fingerprint design is untouched.
- Links use the absolute-from-root convention.

## Success criteria

1. Every edit listed is present; no remaining spec requires 0002 to query link storage directly for list/get/disable.
2. Error mapping table covers every code in the catalog error table (checked by hand against `docs/commands/README.md` section 1).

## Out of scope / risks

- **Design problem (unresolved):** `link.update`/`enable`/`disable` take `slug`, but 0002 addresses pairs by `url_pair_id`. Either the bridge resolves id -> slug with `link.get {id}` first (two commands, race window) or the catalog extends those commands with an `id` alternative (changes 03.0). Recommend extending the catalog (exactly one of `slug`/`id`, as `link.get` already does) - recorded as open decision D1 in the decision log; apply the user's outcome here.
- `page`/`page_size` cannot express offsets that are not multiples of `page_size`; acceptable.
