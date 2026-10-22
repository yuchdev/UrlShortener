# 08 - link.get, link.preview, link.stats commands

**Parent task:** 03.0 Link and browse commands on the registry
**State:** ⬜ Not started
**Depends on:** 03.0/05 (codec), 01.0/05-07 (`ArgSpec`, descriptor, `execute`)
**Blocks:** 11, 04.0/03, 06.0/13
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Register the three read commands whose handlers are pure pass-throughs to
`LinkCommandService`, and pin the two result shapes that differ from `LinkView` today:
`link.preview` returns the **reduced** preview body, and `link.stats` returns either the
range report (when `from`, `to`, `bucket` are all given) or the lifetime-counters body (otherwise),
exactly as `GET /api/v1/links/{slug}/stats` does now.

## Dependencies

- Codec: `decode_slug_or_id`, `link_view_result`, `link_preview_result`, `link_stats_result` (subtask 05).
- Verified service API: `GetLink(GetLinkQuery{by, value})`, `PreviewLink(GetLinkQuery)`,
  `GetLinkStats(GetLinkStatsQuery{slug, from, to, bucket})`; serializers `serializeLinkViewJson`,
  `serializeLinkPreviewJson` (field order `slug,url,status,redirect_type,enabled,expires_at,deleted_at`),
  `serializeLinkStatsJson`.
- Verified REST behavior to reproduce (`link_handlers.cpp::handleLinkStats`): when
  `from`, `to` and `bucket` are all non-empty -> `GetLinkStats`; otherwise a lifetime body
  `{"slug","total_redirects","redirects_24h","redirects_7d","last_accessed_at"(string|null),"created_at"}`
  built inline from `getLinkForRead` (404 `not_found` when the slug is unknown). `from`/`to` are
  decimal **epoch seconds** (`LegacyLinkStatsReader::parseEpoch`), not RFC 3339 as the REST route docs say;
  bucket is `hour|day|week`; failures use details `invalid_timestamp` / `invalid_bucket`.
- **Catalog correction needed:** 3.1 says `link.preview`/`link.stats` results are `LinkView`/`LinkStatsView`.
  The accurate statement is the reduced preview projection and the two-shape stats result above
  (record in Task 08.0; do not change shapes, contract C3).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/app/link_command_service.hpp` | modify | Declare `std::string serializeLinkLifetimeStatsJson(const LinkView&)` next to the other serializers. |
| `src/app/link_command_service.cpp` | modify | Implement it (moved verbatim from `handleLinkStats`; REST switches to it in Task 04.0). |
| `include/url_shortener/commands/link_commands.hpp` | modify | Declare `make_link_get_descriptor`, `make_link_preview_descriptor`, `make_link_stats_descriptor`. |
| `src/commands/link_read_commands.cpp` | create | Three descriptors and handlers. Add to `COMMON_CPP_SOURCES`. |
| `tests/unit/app/42_serialize_link_lifetime_stats_json.cpp` | create | Serializer field order/escaping. |
| `tests/unit/commands/22_link_read_commands.cpp` | create | Handler tests. |
| `sources.cmake` | modify | `APP_UNIT_SOURCES` += 42; `COMMON_CPP_SOURCES`, `COMMANDS_UNIT_SOURCES` as usual. |

## Classes and functions

```cpp
// app (new, same style as serializeLinkStatsJson)
std::string serializeLinkLifetimeStatsJson(const LinkView& link);
// -> {"slug":..,"total_redirects":N,"redirects_24h":N,"redirects_7d":N,
//     "last_accessed_at":"..."|null,"created_at":".."}

// commands
CommandDescriptor make_link_get_descriptor(app::LinkCommandService&);      // link.get
CommandDescriptor make_link_preview_descriptor(app::LinkCommandService&);  // link.preview
CommandDescriptor make_link_stats_descriptor(app::LinkCommandService&);    // link.stats
```

Descriptors: all `permission = Permission::read`, `kind = ExecutionKind::sync`, `mutating = false`, `supports_dry_run = false`; `maps_to` = the REST route and `link <verb>` per the catalog.

| command | args | handler |
|---|---|---|
| `link.get` | `slug` string, `id` string (both `one_of_group = "slug_or_id"`; registry enforces exactly one) | `decode_slug_or_id` -> `GetLink` -> `link_view_result` |
| `link.preview` | `slug`, `id` (same `one_of_group`) | `decode_slug_or_id` -> `PreviewLink` -> `link_preview_result` |
| `link.stats` | `slug` (required), `from`, `to` (strings of decimal epoch seconds, optional), `bucket` (**string**, documented as `hour`\|`day`\|`week`, optional) | if `from`,`to`,`bucket` all present: `GetLinkStats` -> `link_stats_result`; else `GetLink({slug})` then `serializeLinkLifetimeStatsJson` |

Edge: partial range (e.g. only `from`) behaves like REST today -> lifetime body, the extra args ignored.
Error mapping goes through `to_command_error`; `invalid_timestamp` / `invalid_bucket` details become
`invalid_field` with that detail as the message (see 01.0/02 note; Task 04.0 re-promotes them to REST codes).

## Implementation steps

1. Add and unit-test `serializeLinkLifetimeStatsJson` (app layer first, no registry needed).
2. Add the three descriptors/handlers in `link_read_commands.cpp`.
3. Add header declarations, sources, tests; build and run both tests.
4. Do **not** edit `link_handlers.cpp` here (Task 04.0 swaps it to the new serializer).

## Commands

```powershell
cmake --build cmake-build --target app__42_serialize_link_lifetime_stats_json commands__22_link_read_commands
ctest --test-dir cmake-build -R "^(app__42_serialize_link_lifetime_stats_json|commands__22_link_read_commands)$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/app/42_serialize_link_lifetime_stats_json.cpp` | `app__42_serialize_link_lifetime_stats_json` | unit | key order exactly as above; `last_accessed_at` null vs string; slug containing quote/backslash/unicode is escaped (`jsonString`); counters at 0 and at `UINT64_MAX`. |
| `tests/unit/commands/22_link_read_commands.cpp` | `commands__22_link_read_commands` | unit | `link.get` by slug and by id equal `serializeLinkViewJson`; both/neither selector -> `invalid_field` with message ending `one_of_violation`; unknown slug/id -> `not_found`; `link.preview` equals `serializeLinkPreviewJson` for enabled, disabled, expired (`expires_at` in the past) and deleted links (`status` field); `link.preview` by id works; `link.stats` range mode returns `serializeLinkStatsJson` from `FakeLinkStatsReader`; reader error `invalid_timestamp` -> `invalid_field` message `invalid_timestamp`; a `bucket` outside `hour|day|week` -> `invalid_field` with message `invalid_bucket` from the reader (deliberately **not** an `enumeration` ArgSpec: the REST route reports `invalid_bucket` and the registry would pre-empt it with `bucket: not_in_enum`, breaking contract C3); lifetime mode for 0, 1 and 2 of the 3 range args; lifetime mode for unknown slug -> `not_found`; read-only caller (`ControlSet::User`) is allowed for all three. |

Manual check: none.

## Constraints

- C3: shapes unchanged; `serializeLinkLifetimeStatsJson` must emit byte-identical text to the current inline code.
- C1: the lifetime body is built from `LinkView.stats` and `created_at` returned by `GetLink`; no direct
  `getLinkForRead` / singleton access from `src/commands/` (also required by Task 02.0's single backend).
- Read commands never mutate and never touch the cache write path.
- Do not parse `from`/`to` in the handler; validation stays in `ILinkStatsReader` (one place).
- C++17; no JSON library; new `.cpp` in `COMMON_CPP_SOURCES`; tests in `sources.cmake`.
- ArgSpec string caps follow [07](07-link-create-command.md) (`max_length = 65536`) so the service remains the only validator.

## Success criteria

1. Both tests pass.
2. `link.preview` result for a disabled link contains `"status":"disabled"` and no `short_url`/`tags` keys (byte-equal to `serializeLinkPreviewJson`).
3. `link.stats` without range returns the six-key body; with a full range returns `serializeLinkStatsJson`.
4. `grep -rn "getLinkForRead\|linkRepository" src/commands` is empty.

## Out of scope / risks

- `bucket` is a free `string` ArgSpec so that validation (and the legacy `invalid_bucket` code) stays in `ILinkStatsReader` only; `command.list` therefore cannot offer a bucket picker enumeration - the TUI hard-codes the three values from the arg `doc`.
- Lifetime-mode and range-mode sharing one command is a pragmatic carry-over of REST behavior; if the
  product later wants two commands (`link.stats` and `link.counters`), change the catalog and REST together.
- Soft-deleted links: REST lifetime stats today read via `getLinkForRead` and return the body even for a
  deleted link; `GetLink` returns deleted links too (verified: `findBySlug` has no deleted filter), so parity holds. UNVERIFIED for the repository-backed store of Task 02.0 - the parity test in subtask 11 must cover it.
