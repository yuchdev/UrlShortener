# 14 - link.list command

**Parent task:** 03.0 Link and browse commands on the registry
**State:** ⬜ Not started
**Depends on:** 03.0/12 (`ILinkLister`), 03.0/11 (header and test-fixture conventions for link commands), 01.0/05-07
**Blocks:** 18, 04.0/07, 05.0/10, 06.0/12, 06.0/17
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Expose the browse query as the `link.list` command: decode and bound the arguments, call
`app::ILinkLister::list`, return `serializeLinkListJson`. This is what the TUI Links screen, the
future `GET /api/v1/links` (Task 04.0 subtask 07) and Milestone 0002's pair list consume.

## Dependencies

- Subtask 12 (`LinkListQuery`, `ILinkLister`, `serializeLinkListJson`, bounds constants); subtask 13 supplies the default-mode lister
  but is not needed to unit-test the command (fake lister).
- 01.0/05: `ArgSpec` supports `min`/`max` for integers, `enumeration`, `max_length`; bounds are therefore declared in the descriptor
  *and* re-checked by `validate_link_list_query` (defense in depth).
- Catalog 3.2 argument table (with `expired` added to `status`, see subtask 12) and Q9 default `status=active`.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/browse_commands.hpp` | create (or modify if 15 landed first) | Declares `make_link_list_descriptor(app::ILinkLister&)`; forward-declares `app::ILinkLister`. |
| `src/commands/link_list_command.cpp` | create | Descriptor and handler. Add to `COMMON_CPP_SOURCES`. |
| `tests/unit/commands/26_link_list_command.cpp` | create | Command tests over a fake lister. |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES`, `COMMANDS_UNIT_SOURCES`. |

## Classes and functions

```cpp
namespace url_shortener::commands {
/// Builds `link.list`. @p lister must outlive the registry.
CommandDescriptor make_link_list_descriptor(app::ILinkLister& lister);
}
```

Descriptor: `name = "link.list"`, `permission = Permission::read`, `kind = ExecutionKind::sync`, `mutating = false`,
`supports_dry_run = false`, `maps_to = {"GET /api/v1/links", "link list"}`.

| arg | ArgType | default | bounds |
|---|---|---|---|
| `status` | enumeration `all,active,disabled,expired,deleted` | `active` | |
| `q` | string | - | `max_length = 256` |
| `tag` | string | - | |
| `campaign` | string | - | |
| `sort` | enumeration `created_at,slug,updated_at` | `created_at` | |
| `order` | enumeration `asc,desc` | `desc` | |
| `offset` | integer | 0 | `min = 0` |
| `limit` | integer | 50 | `min = 1`, `max = 500` |

Handler: build `app::LinkListQuery` (absent args keep the struct defaults, `order` -> `descending`), call
`lister.list(query)`, return `CommandResult{serializeLinkListJson(*page), {}, ""}` or `to_command_error(result.error)`.
`audit_target` stays empty (read command).

## Implementation steps

1. Create/extend `browse_commands.hpp`.
2. Write `link_list_command.cpp` (ArgSpec rows, defaults documented via `default_value`, handler).
3. Write the test with an in-test `FakeLinkLister` that records the received `LinkListQuery`.
4. Register in `sources.cmake`; build; run.

## Commands

```powershell
cmake --build cmake-build --target commands__26_link_list_command
ctest --test-dir cmake-build -R "^commands__26_link_list_command$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/26_link_list_command.cpp` | `commands__26_link_list_command` | unit | no args -> defaults reach the lister (`active`, `created_at`, descending, offset 0, limit 50); every arg decoded to the right field (including `order:"asc"`); `limit:500` accepted, `limit:501`, `limit:0`, `offset:-1` -> `invalid_field` (`limit: out_of_range`); `status:"bogus"`, `sort:"nope"`, `order:"up"` -> `invalid_field` (`not_in_enum`); `q` of 256 chars accepted, 257 -> `invalid_field` (`too_long`); unknown arg -> `invalid_field`; wrong type (`limit:"5"`) -> `invalid_field`; lister error propagated with its code; result text equals `serializeLinkListJson` of the fake's page; empty page is valid JSON; `ControlSet::User` allowed (read permission); the handler never calls the lister when validation fails. |

Manual check (after Task 04.0 and a SQLite-backed server): `curl "$BASE/api/v1/links?status=all&limit=5"` returns 5 items and a numeric `total`.

## Constraints

- C1/C3: exactly one handler; item shape is `serializeLinkViewJson` (via the lister's serializer).
- C6: bounded `q` and `limit`; no new fields exposed.
- C10: the redirect path does not use this command.
- C++17; naming per `.clang-tidy`; new `.cpp` in `COMMON_CPP_SOURCES`; no storage includes under `include/url_shortener/commands/`.

## Success criteria

1. `commands__26_link_list_command` passes.
2. `limit` 501 is rejected by the registry before the lister runs (fake records zero calls).
3. A page produced by the repository lister (subtask 12) round-trips through this command unchanged (covered in the runtime test of subtask 18).

## Out of scope / risks

- Cursor pagination, multi-column sort and `owner` filtering are not offered.
- Default `status=active` hides disabled/expired/deleted links; the TUI sends `status=all` explicitly (Q9).
