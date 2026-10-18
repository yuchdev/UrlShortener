# 09 - link.update command

**Parent task:** 03.0 Link and browse commands on the registry
**State:** ⬜ Not started
**Depends on:** 03.0/05 (codec: `patch_state_of`, `decode_campaign`), 03.0/06 (`ValidateUpdateLink`), 01.0/05-07
**Blocks:** 11, 04.0/05
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Define `link.update` with PATCH semantics identical to `app::UpdateLinkCommand` and
`PATCH /api/v1/links/{slug}`: an absent argument leaves the field untouched, an explicit
JSON `null` clears `expires_at` / `campaign`, a value replaces. The handler only decodes
the three states into the existing double-optional fields; all validation stays in
`LinkCommandService::UpdateLink`.

## Dependencies

- `app::UpdateLinkCommand` (verified): `slug`, `std::optional<bool> enabled`,
  `std::optional<std::optional<std::string>> expires_at`, `std::optional<std::vector<std::string>> tags`,
  `std::optional<std::unordered_map<std::string,std::string>> metadata`,
  `std::optional<std::optional<Link::Campaign>> campaign`.
- Service errors (verified in `UpdateLink`): `not_found`, `invalid_field` (messages
  `expires_at must be RFC3339 UTC or null`, `tags violate constraints`, `metadata must be flat object with string values`,
  `campaign fields exceed limits`), `storage_failure`.
- Verified in the 01.0/05 spec: `ArgSpec::nullable` accepts JSON `null`, and `ArgsView::has` / `is_null` distinguish absent from
  `null`; this is exactly what `link.update` needs. If that spec changes, `link.update` is blocked (it cannot be faithful without it).
- Ordering fact for Task 04.0: the REST handler checks existence (404) **before** validating the body;
  `execute` validates arguments first. The REST projection (04.0/05) preserves REST order itself.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/link_commands.hpp` | modify | Declare `make_link_update_descriptor`. |
| `src/commands/link_update_command.cpp` | create | Descriptor and handler. Add to `COMMON_CPP_SOURCES`. |
| `tests/unit/commands/23_link_update_command.cpp` | create | Tri-state and error tests. |
| `sources.cmake` | modify | Standard additions. |

## Classes and functions

```cpp
CommandDescriptor make_link_update_descriptor(app::LinkCommandService& service);
```

`name = "link.update"`, `permission = Permission::write`, `kind = ExecutionKind::sync`, `mutating = true`, `supports_dry_run = true`,
`maps_to = {"PATCH /api/v1/links/{slug}", "link update"}`.

| arg | type | required | nullable | semantics |
|---|---|---|---|---|
| `slug` | string | yes | no | link to update |
| `enabled` | bool | no | no | absent = untouched |
| `expires_at` | timestamp | no | **yes** | absent = untouched; `null` clears |
| `tags` | list<string> | no | no | replaces the whole list; `[]` removes all |
| `metadata` | map<string,string> | no | no | replaces the whole map; `{}` removes all |
| `campaign` | object | no | **yes** | absent = untouched; `null` clears |

ArgSpec size caps: as in [07](07-link-create-command.md) (`max_length = 65536`, `max_items = 4096`) so the service stays the single validator and REST
messages do not change for oversized input.

Handler:

```cpp
CommandResult handle_link_update(app::LinkCommandService& service,
                                 CommandContext& context, const ArgsView& args);
// cmd.slug = args.get_string("slug")
// enabled: if has("enabled") cmd.enabled = get_bool
// expires_at: switch (patch_state_of(args,"expires_at")) {
//     absent: leave nullopt;  null_value: cmd.expires_at = std::optional<std::string>{};
//     value: cmd.expires_at = std::optional<std::string>{get_string} }
// tags/metadata: if has() -> assign (empty list/map allowed)
// campaign: absent / null_value -> std::optional<Link::Campaign>{} / value via decode_campaign
// result = context.dry_run ? service.ValidateUpdateLink(cmd) : service.UpdateLink(cmd);
// return link_view_result(result, cmd.slug);   // sets audit_target
```

## Implementation steps

1. Add the descriptor with the nullable ArgSpec flags.
2. Implement the handler exactly as outlined; no validation beyond decoding.
3. Write the tests, register, build, run.

## Commands

```powershell
cmake --build cmake-build --target commands__23_link_update_command
ctest --test-dir cmake-build -R "^commands__23_link_update_command$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/23_link_update_command.cpp` | `commands__23_link_update_command` | unit | only `slug` -> ok, nothing changed except `updated_at`; `enabled:false` then `true`; `expires_at` value / `null` clears / absent keeps (three separate cases, assert stored link); `campaign` value / `null` clears / absent keeps; `tags:[]` clears, `tags` replacement, `metadata:{}` clears; unknown slug -> `not_found`; bad `expires_at` -> `invalid_field`; too many tags / oversize metadata / oversize campaign -> `invalid_field`; unknown key (`deleted_at`, `target_url`) -> `invalid_field`; wrong type (`enabled:"yes"`) -> `invalid_field` naming `enabled`; `dry_run` -> ok view reflects changes, stored link unchanged; `ControlSet::User` -> `not_authorized`, store unchanged; forced update failure (`forceUpdateStorageFailure`) -> `storage_failure`; result text equals `serializeLinkViewJson` of the stored link after the update. |

Manual check: none.

## Constraints

- C3: absent vs `null` vs value must stay three distinct states (the REST handler's
  `hasJsonField` + `extractJsonValueToken` distinction); never collapse the optional layers.
- Validation is not duplicated: only `LinkCommandService::UpdateLink` validates tags/metadata/campaign/expiry.
- C6: errors and audit lines never include the new values.
- `target_url` is immutable through this command (same as REST PATCH today).
- C++17, no JSON library, naming per `.clang-tidy`, `.cpp` in `COMMON_CPP_SOURCES`.

## Success criteria

1. `commands__23_link_update_command` passes with all three states covered for both nullable args.
2. A PATCH-equivalent request produces text byte-equal to `serializeLinkViewJson` of the persisted link.
3. `grep -n "validateTags\|validateMetadata\|validateCampaign" src/commands` is empty.

## Out of scope / risks

- REST per-field 400 codes (`invalid_expires_at`, `invalid_tags`, ...) and existence-before-validation
  ordering: Task 04.0 subtask 05.
- Optimistic concurrency / `version` precondition: not in the current service; out of scope.
