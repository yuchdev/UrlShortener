# 04 - REST create through the registry

**Parent task:** 04.0 REST and CLI adapters
**State:** ⬜ Not started
**Depends on:** 02 (bridge), 01 (args builder), 03.0/11 (`link.create` registered, parity gate)
**Blocks:** 06
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Make `POST /api/v1/links` and the compatibility `POST /api/v1/short-urls` build a `link.create` request and call
the registry. All REST-only behavior - field-name aliases, per-field 400 codes and their **order** - stays in
`createLinkFromRequest`, because it is part of the public REST contract; only the DTO construction and the
service call/mapping disappear.

## Dependencies

- Subtask 02 (`execute_resource_command`, `resource_response(…, 201)`), subtask 01 (`args_builder`), 03.0/07 (`link.create` with canonical arg `target_url`).
- Verified REST behavior to preserve (`createLinkFromRequest`, `src/http/handlers/link_handlers.cpp`; decision-log Q18):
  1. target: `url` first, else `target_url`; neither -> `400 invalid_url` "url is required".
  2. slug: `slug`; on the compat route (`CreateSlugFieldMode::slug_or_code`) also `code` when `slug` is absent; `code` is ignored on the canonical route.
  3. `redirect_type` not `temporary|permanent` -> `400 invalid_redirect_type`.
  4. `expires_at` not RFC 3339 UTC -> `400 invalid_expires_at`.
  5. `enabled` present but not a bool -> `400 invalid_enabled`.
  6. `tags` present but not a string array -> `400 invalid_tags`.
  7. `metadata` present but not a flat string object -> `400 invalid_metadata`.
  8. `campaign` present: `null` -> no campaign; not an object -> `400 invalid_campaign`; an object -> the six known string members, other keys ignored.
  9. `deleted_at` present -> `400 invalid_request` "deleted_at is server-managed".
  10. Unknown top-level keys are **ignored** (the registry would reject them; this is why args are rebuilt from known keys).
  11. Success: `201` + `serializeLinkViewJson`. Service errors: statuses/codes via the bridge (409 for `reserved_slug` and `slug_conflict`, 400 `invalid_url`/`invalid_slug`, 400 `invalid_request` for `invalid_field`, 500 `repository`).
- The checks above use the quirky text-scanning helpers (`extractJsonStringField` finds the first `"key"` anywhere in the body). They are the current contract and are reused unchanged; replacing them with `scan_object` would change behavior for hostile bodies and is out of scope.
- ArgSpec caps (03.0/07) are generous so the service, not the registry, stays the validator for oversized input.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `src/http/handlers/link_handlers.cpp` | modify | Rewrite the tail of `createLinkFromRequest` (after the per-field checks): build args, `execute_resource_command`, `resource_response(..., 201)`. Remove `app::CreateLinkCommand` use and `makeCommandService` from this path. |
| `tests/unit/http/17_rest_create_via_registry.cpp` | create | Characterization + edge cases (written first against the old code). |
| `sources.cmake` | modify | `HTTP_UNIT_SOURCES` += the test. |

Unchanged: `include/url_shortener/http/handlers/link_handlers.hpp` (signature and `CreateSlugFieldMode`), `compatibility_handlers.cpp` (calls `createLinkFromRequest`).

## Classes and functions

No new public symbols. Inside `createLinkFromRequest`, after the legacy checks have produced locals (`raw_url`, optional `slug`,
`redirect_type_raw`, `expires_at_raw`, `enabled_raw`, `tags`, `metadata`, `campaign`):

```cpp
commands::args_builder args;
args.add_string("target_url", *raw_url);
if (slug)             args.add_string("slug", *slug);
if (redirect_type_raw) args.add_string("redirect_type", *redirect_type_raw);
if (expires_at_raw)    args.add_string("expires_at", *expires_at_raw);
if (enabled_raw)       args.add_bool("enabled", *enabled_raw);
if (tags)              args.add_string_list("tags", *tags);
if (metadata)          args.add_string_map("metadata", std::map<std::string,std::string>(metadata->begin(), metadata->end()));
if (campaign_present)  campaign_is_null ? args.add_null("campaign") : args.add_raw("campaign", campaign_object_text);
// campaign_object_text: a second args_builder with only the known members that are present
const auto response = execute_resource_command(config, "link.create", args.str(), request_id_of(req));
return resource_response(req, config, is_tls, response, 201);
```

`request_id_of(req)` is the existing request-id extraction used by `makeApiErrorResponse` (`requestIdFromRequest`); if it is file-local to
`request_handlers.cpp`, expose it through `request_handlers.hpp` (one declaration) rather than duplicating it.

## Implementation steps

1. **Characterize first:** write `17_rest_create_via_registry.cpp` and run it against the unmodified code; green.
2. Rewrite the tail of `createLinkFromRequest`; leave the ordered per-field checks byte-for-byte as they are.
3. Delete the now-unused includes/locals (`app::CreateLinkCommand`).
4. Run the new test, `10_link_handlers`, `11_compatibility_handlers`, `02_endpoint_matrix_characterization`, `03_method_and_error_characterization`.

## Commands

```powershell
cmake --build cmake-build --target 17_rest_create_via_registry 10_link_handlers 11_compatibility_handlers 02_endpoint_matrix_characterization 03_method_and_error_characterization
ctest --test-dir cmake-build -R "^(17_rest_create_via_registry|10_link_handlers|11_compatibility_handlers|02_endpoint_matrix_characterization|03_method_and_error_characterization)$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/http/17_rest_create_via_registry.cpp` | `17_rest_create_via_registry` | unit | success `201` with only `url`; with only `target_url`; with both (the `url` value wins, assert stored target); response body target key is `url`; all optional fields round-trip (tags, metadata, campaign with six keys, `redirect_type`, `expires_at`, `enabled:false`); `campaign:null`, `campaign:{}`, campaign with an unknown inner key (ignored); unknown top-level key ignored (still 201); no url -> `400 invalid_url`; each per-field error code from the list above, **and the order** (a body with bad `redirect_type` and bad `tags` reports `invalid_redirect_type`; missing url wins over everything); `deleted_at` -> `400 invalid_request`; 21 tags -> `400 invalid_request` "tags violate constraints"; 5000-character URL -> 201; reserved slug -> 409 `reserved_slug`; duplicate slug -> 409 `slug_conflict`; private target without `shortener_allow_private_targets` -> 400 `invalid_url`; compat route: `code` accepted, `slug` wins over `code`, canonical route ignores `code`; a hostile body (quotes/newlines in url) is stored escaped and returned as valid JSON; `X-Request-Id` present on success and error. |

Manual check: `curl -s -X POST localhost:8000/api/v1/links -d '{"url":"https://example.com/a"}'` on this branch and on `master` return the same fields (ids and timestamps differ).

## Constraints

- C3: the 400 codes and their order are the contract; the existing characterization tests are not edited.
- C1: after this change `createLinkFromRequest` has no DTO and no service call; it may validate request *shape* to choose REST error codes only.
- Q18: canonical arg name is `target_url`; the `url` alias lives only here.
- Untrusted input is escaped by `args_builder`; never concatenated.
- C10 / no other handler is touched; C++17; naming per `.clang-tidy`.

## Success criteria

1. All five targets pass without editing the pre-existing tests.
2. `grep -n "CreateLinkCommand\|makeCommandService\|CreateLink(" src/http/handlers/link_handlers.cpp` shows no use in `createLinkFromRequest`.
3. A POST whose body contains an unknown key still returns 201 (test).

## Out of scope / risks

- Fixing the text-scanning quirks of `extractJson*` (a field name inside a string value can be misread); documented, behavior-preserving.
- The compat route keeps its own request-body documentation (`url`/`code`); no doc change here.
