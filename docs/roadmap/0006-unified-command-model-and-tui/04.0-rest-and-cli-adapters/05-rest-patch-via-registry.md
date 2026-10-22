# 05 - REST PATCH through the registry

**Parent task:** 04.0 REST and CLI adapters
**State:** ⬜ Not started
**Depends on:** 02 (bridge), 01 (args builder), 03.0/11 (`link.update`, `link.get` registered, parity gate), 03.0/09
**Blocks:** 06
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Make `PATCH /api/v1/links/{slug}` run `link.update` through the registry while keeping the REST-only parts of
its contract: **existence is checked before the body is looked at**, the per-field 400 codes and messages are
produced in the same order, and absent / `null` / value stay three distinct states.

## Dependencies

- Subtasks 01-02, 03.0/09 (`link.update`; nullable `expires_at` and `campaign`).
- Verified REST behavior (`handlePatchLink`, `src/http/handlers/link_handlers.cpp`), in order:
  1. Lookup through `GetLink` first: unknown slug -> `404 not_found` even when the body is garbage (comment in the code: contract C3 of Milestone 0003).
  2. `enabled` present but not bool -> `400 invalid_enabled` "enabled must be boolean".
  3. `expires_at`: token missing -> `400 invalid_expires_at`; `null` -> clear; string that parses with `parseRfc3339Zulu` -> set; anything else -> `400 invalid_expires_at` "expires_at must be RFC3339 UTC or null".
  4. `tags`: not a string array -> `400 invalid_tags` "tags must be string array"; fails `validateTags` -> `400 invalid_tags` "tags violate constraints"; the normalized (trimmed, de-duplicated) list is what is applied.
  5. `metadata`: not a flat string object, or fails `validateMetadata` -> `400 invalid_metadata` "metadata must be flat object with string values".
  6. `campaign`: `null` -> clear; not an object -> `400 invalid_campaign` "campaign must be object or null"; fails `validateCampaign` -> `400 invalid_campaign` "campaign fields exceed limits".
  7. Then `UpdateLink`; `200` + `serializeLinkViewJson`; service errors via the bridge.
  8. Unknown top-level keys are ignored; an empty or non-JSON body updates nothing and still returns 200 with the refreshed `updated_at`.
- The validators `validateTags`, `validateMetadata`, `validateCampaign` are the same functions the service calls; calling them in the handler is REST error-code selection (the 0003 handler does the same), not duplicated business logic.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `src/http/handlers/link_handlers.cpp` | modify | Rewrite `handlePatchLink`: existence check via the bridge (`link.get`), keep the ordered per-field checks, build args, `execute_resource_command("link.update")`, `resource_response(..., 200)`. |
| `tests/unit/http/18_rest_patch_via_registry.cpp` | create | Characterization + edge cases (written first against the old code). |
| `sources.cmake` | modify | `HTTP_UNIT_SOURCES` += the test. |

## Classes and functions

No new public symbols. Handler outline:

```cpp
const auto slug = pathValue(context, "slug");
// 1. existence first
{ commands::args_builder a; a.add_string("slug", slug);
  const auto existing = execute_resource_command(config, "link.get", a.str(), request_id);
  if (!existing.ok) return resource_response(req, config, is_tls, existing); }
// 2..6. unchanged ordered checks -> locals (enabled, expires_at tri-state, tags, metadata, campaign tri-state)
commands::args_builder args;  args.add_string("slug", slug);
if (enabled)            args.add_bool("enabled", *enabled);
if (expires_present)    expires_is_null ? args.add_null("expires_at") : args.add_string("expires_at", *expires);
if (tags)               args.add_string_list("tags", *tags);          // normalized list
if (metadata)           args.add_string_map("metadata", ...);
if (campaign_present)   campaign_is_null ? args.add_null("campaign") : args.add_raw("campaign", campaign_text);
return resource_response(req, config, is_tls, execute_resource_command(config, "link.update", args.str(), request_id), 200);
```

The three-state locals replace the `std::optional<std::optional<...>>` DTO fields that the old code assigned; the
command/registry now carries the same distinction (`ArgsView::has` / `is_null`, 01.0/05).

## Implementation steps

1. **Characterize first:** write `18_rest_patch_via_registry.cpp`, run against the unmodified code; green.
2. Rewrite `handlePatchLink`; keep the ordered checks verbatim.
3. Run the new test, `10_link_handlers`, `02_endpoint_matrix_characterization`, `03_method_and_error_characterization`.

## Commands

```powershell
cmake --build cmake-build --target 18_rest_patch_via_registry 10_link_handlers 02_endpoint_matrix_characterization 03_method_and_error_characterization
ctest --test-dir cmake-build -R "^(18_rest_patch_via_registry|10_link_handlers|02_endpoint_matrix_characterization|03_method_and_error_characterization)$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/http/18_rest_patch_via_registry.cpp` | `18_rest_patch_via_registry` | unit | `enabled` false then true; `expires_at` set / `null` clears / absent keeps (three cases, verify via GET); `campaign` set / `null` clears / absent keeps / `{}`; `tags` replaced with normalization (`[" a ","a","b"]` -> `["a","b"]`), `tags:[]` clears; `metadata:{}` clears; unknown slug with a valid body -> 404; unknown slug with a **malformed** body -> still 404 (existence first); each per-field 400 code and message; field order (bad `enabled` + bad `tags` -> `invalid_enabled`); 21 tags -> `400 invalid_tags` "tags violate constraints"; metadata value 513 chars -> `400 invalid_metadata`; empty body `{}`, non-JSON body and unknown keys -> 200 with `updated_at` refreshed and nothing else changed; response equals `serializeLinkViewJson` of the stored link; `target_url`/`url` in the body is ignored (target unchanged). |

Manual check: compare `curl -X PATCH` against `master` for a null-clear request.

## Constraints

- C3: existence-before-validation order, codes, messages, "unknown keys ignored" and "garbage body is a no-op" are all preserved; characterization tests are not edited.
- C1: no `UpdateLinkCommand` or `LinkCommandService` use remains in this handler.
- The two registry calls (`link.get`, `link.update`) are not atomic - same as the old two service calls.
- C++17; no new JSON code beyond `args_builder`; naming per `.clang-tidy`.

## Success criteria

1. The four test targets pass; pre-existing tests unmodified.
2. `grep -n "UpdateLinkCommand\|GetLink(" src/http/handlers/link_handlers.cpp` is empty in `handlePatchLink`.
3. Malformed-body-on-unknown-slug returns 404 (test).

## Out of scope / risks

- Optimistic concurrency / `If-Match`: not in the contract.
- A future change that moves existence checking into the registry would alter REST ordering; keep it in the adapter.
