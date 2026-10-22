# 11 - Corpus: link commands

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 01, 04; Task 03.0 (nine link commands registered)
**Blocks:** 15
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

Author the corpus files for `link.create/get/update/delete/enable/disable/restore/preview/stats`:
every argument, every documented error, PATCH semantics (absent vs `null`),
and the REST resource-route equivalents, so the nine commands are pinned on all
six transports.

## Dependencies

- Subtasks 01 (schema), 04 (to run and debug cases quickly).
- Catalog section 3.1 (args, errors) - the case list below is derived from it, not from the implementation.
- **Result shape comes from the serializers, not the catalog prose.** `serializeLinkViewJson` (`src/app/link_command_service.cpp`) emits: `id, slug, url, short_url, created_at, updated_at, status, redirect_type, tags, metadata, campaign, stats{total_redirects, redirects_24h, redirects_7d, last_accessed_at}` - the target is `url` (args use `target_url`; REST accepts both `url` and `target_url`), lifecycle is `status` (not `enabled`), and there is no `expires_at` key in the view. Task 03.0 README records further catalog corrections (preview uses the reduced `serializeLinkPreviewJson`; stats has two shapes; `from`/`to` are epoch seconds and `bucket` is `hour|day|week`; REST maps `reserved_slug` to 409). Write expectations from `tests/unit/app/11_serialize_link_view_json_fields.cpp` and Task 03.0's pinned shapes.
- Existing behavior oracles (cases must agree): `tests/unit/app/13_link_command_service_update_field_semantics.cpp`, `tests/integration/link_management/*`, `tests/unit/http/02_endpoint_matrix_characterization.cpp`.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/conformance/commands/10_link_create.json` | create | create cases |
| `tests/conformance/commands/11_link_read.json` | create | get, preview, stats |
| `tests/conformance/commands/12_link_update.json` | create | update PATCH semantics |
| `tests/conformance/commands/13_link_state.json` | create | delete, enable, disable, restore |
| `tests/conformance/commands/README.md` | modify | Link the file list |

## Classes and functions (cases)

Case names are `<command>_<scenario>`; every case sets `rest_resource` on the steps where a route exists (catalog section 5).

**10_link_create.json (14 cases):** `link_create_minimal` (generated slug `<slug:N>`, `redirect_type` default `temporary`, `status` `active`, `short_url` ends with the slug, `stats` all zero / `last_accessed_at` null); `link_create_custom_slug`; `link_create_all_fields` (tags, metadata, campaign object, `expires_at`, `redirect_type: permanent`); `link_create_url_alias_rest_only` (REST resource body key `url` instead of `target_url`; both accepted, `url` wins when both present - Task 03.0 README; runs only on `rest_resource`); `link_create_invalid_url` (not a URL); `link_create_private_target_blocked` (`http://127.0.0.1/` -> `invalid_url`, identical to malformed: assert both produce the same `error.code` and no distinguishing field); `link_create_invalid_slug`; `link_create_reserved_slug` (`api`; HTTP 409 on REST per `statusForAppError`, set via the step's `rest_resource.status`; the generic endpoint status comes from the 01.0 table); `link_create_slug_conflict` (second create -> `slug_conflict`); `link_create_unknown_arg` (`invalid_field`); `link_create_wrong_type` (`enabled: "yes"` -> `invalid_field`); `link_create_missing_target` (`invalid_field`); `link_create_dry_run` (`options.dry_run`: no row created; follow-up `link.get` -> `not_found`; skip on `rest_resource`); `link_create_expired_at_past` only if catalog/ADR rejects it (**UNVERIFIED**; delete the case if behavior is accept).

**11_link_read.json (10 cases):** `link_get_by_slug` (exact full-shape expectation, doubles as serializer parity check); `link_get_by_id` (id captured via `save`); `link_get_both_slug_and_id` (`invalid_field`); `link_get_neither` (`invalid_field`); `link_get_not_found`; `link_get_deleted_visible_to_management` (soft-deleted link still returned by `link.get`, per `link_management` suite); `link_preview_active` / `link_preview_disabled` / `link_preview_expired` / `link_preview_deleted` (status matrix as `tests/integration/link_management/07_preview_status_matrix_test.py`; result is the reduced preview projection); `link_preview_by_id` (generic transports only: `transports.skip.rest_resource = "by-id preview is generic-only"`); `link_stats_range_mode_empty` (`from`/`to`/`bucket` given as epoch seconds and `hour|day|week`); `link_stats_lifetime_counters_mode` (no range args -> lifetime counters body); `link_stats_invalid_window` (`from` > `to`); `link_stats_invalid_bucket`; `link_stats_not_found`.

**12_link_update.json (10 cases):** `link_update_enabled_only`; `link_update_untouched_fields_preserved` (absent keys unchanged); `link_update_clear_expires_with_null`; `link_update_clear_campaign_with_null`; `link_update_replace_tags`; `link_update_replace_metadata`; `link_update_empty_args_noop_or_invalid` (assert whichever `app::UpdateLinkCommand` does today; read `13_link_command_service_update_field_semantics.cpp` first); `link_update_not_found`; `link_update_unknown_arg`; `link_update_dry_run`.

**13_link_state.json (12 cases):** `link_delete_soft` (follow-up `link.get` shows deleted state), `link_delete_not_found`, `link_delete_twice`, `link_disable_then_preview`, `link_enable_after_disable`, `link_enable_not_found`, `link_restore_after_delete`, `link_restore_not_deleted` (current behavior asserted), `link_restore_not_found`, `link_state_lifecycle_matrix` (create -> disable -> delete -> restore -> enable, asserting each response), `link_enable_missing_slug` (`invalid_field`), `link_delete_blank_slug` (`invalid_field`).

Example (abridged) for the conflict case:

```json
{ "name": "link_create_slug_conflict", "doc": "second create with same slug conflicts",
  "command": "link.create",
  "steps": [
    { "label": "first", "request": {"v":1,"id":"c-1","command":"link.create","args":{"target_url":"https://example.com/a","slug":"dup"}},
      "rest_resource": {"method":"POST","path":"/api/v1/links","body":{"target_url":"https://example.com/a","slug":"dup"},"status":201},
      "expect": {"ok":true,"result":{"id":"<id>","slug":"dup","url":"https://example.com/a","status":"active",
                 "created_at":"<ts>","updated_at":"<ts>","short_url":"<regex:.*/dup>","redirect_type":"temporary"},"match":"subset"} },
    { "label": "second", "request": {"v":1,"id":"c-2","command":"link.create","args":{"target_url":"https://example.com/b","slug":"dup"}},
      "rest_resource": {"method":"POST","path":"/api/v1/links","body":{"target_url":"https://example.com/b","slug":"dup"},"status":409},
      "expect": {"ok":false,"error":{"code":"slug_conflict"}} } ] }
```

## Implementation steps

1. For each case, first write `expect` from the catalog/oracles, then run `direct` (subtask 04); fix the case or file a defect against 03.0 - do not copy actual output blindly.
2. Use `match: "exact"` for results whose full shape is stable (copy the field list from `serializeLinkViewJson`); `subset` only where a field is environment-dependent, with a `doc` note.
3. Run the linter (subtask 01) and all available transports.

## Commands

```bash
python tools/conformance_lint.py tests/conformance/commands
ctest --test-dir cmake-build -L conformance --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| (data files above, executed by existing runners 01, 10, 11, 13, 14, 02) | `conformance__01_direct_registry_corpus` and siblings | `integration;conformance` | about 46 cases x up to 6 transports |

## Constraints

- No literal ids or timestamps (lint rule 6).
- A case must pass on all transports or carry an explicit skip with reason; "works on direct only" is not acceptable.
- Payloads stay under 2 KB; no real network targets (use `example.com` literals; the SSRF check is syntactic and must not resolve DNS).

## Success criteria

1. Every arg in catalog 3.1 appears in at least one success case; every listed error code appears at least once per command that documents it.
2. All 46 (+/- justified) cases pass on every available transport.
3. Linter clean; no `UNVERIFIED` markers left in case `doc` fields.

## Out of scope / risks

- Redirect behavior of the created link (`GET /{slug}`) is covered by `tests/integration/link_management/`, not by the corpus (fast path stays outside the registry).
- Risk: REST legacy field-level 400 codes may differ from catalog codes; the case then documents the divergence (see subtask 09).
