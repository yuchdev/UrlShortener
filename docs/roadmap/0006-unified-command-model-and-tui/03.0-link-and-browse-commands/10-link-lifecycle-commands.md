# 10 - link.delete, link.enable, link.disable, link.restore

**Parent task:** 03.0 Link and browse commands on the registry
**State:** ⬜ Not started
**Depends on:** 03.0/05 (codec), 01.0/05-07
**Blocks:** 11, 04.0/06
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Register the four slug-only mutating lifecycle commands as one handler family over the
existing service methods `DeleteLink` (soft delete), `SetLinkEnabled(true|false)` and
`RestoreLink`. Each returns the `LinkView` JSON the REST lifecycle routes return today.

## Dependencies

- Verified service API (`include/url_shortener/app/link_command_service.hpp`):
  `DeleteLink(DeleteLinkCommand{slug})`, `SetLinkEnabled(SetLinkEnabledCommand{slug, enabled})`,
  `RestoreLink(RestoreLinkCommand{slug})`; all return `Result<LinkView>`; unknown slug -> `not_found`;
  a failed write -> `storage_failure`.
- Verified REST parity targets: `handleDeleteLink` and `handleLifecycleAction` both answer 200 with
  `serializeLinkViewJson`.
- Behavior facts to preserve (verified in `link_command_service.cpp`): delete sets `deleted_at` and
  `updated_at` only (it does not disable the link); restore clears `deleted_at` only; deleting an
  already-deleted link succeeds and overwrites `deleted_at`; enabling a deleted link succeeds (the status
  stays `deleted`). These are today's semantics and are pinned by tests, not changed here.
- The catalog does not list `dry_run` for these commands, so `options.dry_run = true` must produce
  `unsupported` from the registry (01.0/08), not a silent real mutation.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/link_commands.hpp` | modify | Declare `make_link_delete_descriptor`, `make_link_enable_descriptor`, `make_link_disable_descriptor`, `make_link_restore_descriptor`. |
| `src/commands/link_lifecycle_commands.cpp` | create | Four descriptors, one shared slug decoder, four handlers. Add to `COMMON_CPP_SOURCES`. |
| `tests/unit/commands/24_link_lifecycle_commands.cpp` | create | Handler tests. |
| `sources.cmake` | modify | Standard additions. |

## Classes and functions

```cpp
CommandDescriptor make_link_delete_descriptor(app::LinkCommandService&);   // link.delete
CommandDescriptor make_link_enable_descriptor(app::LinkCommandService&);   // link.enable
CommandDescriptor make_link_disable_descriptor(app::LinkCommandService&);  // link.disable
CommandDescriptor make_link_restore_descriptor(app::LinkCommandService&);  // link.restore
```

All: `permission = Permission::write`, `kind = ExecutionKind::sync`, `mutating = true`, `supports_dry_run = false`, single arg `slug`
(string, required), `maps_to` per the catalog (`POST /api/v1/links/{slug}/enable` etc.). File-local helper:

```cpp
namespace {
using lifecycle_call = std::function<app::Result<app::LinkView>(const std::string& slug)>;
CommandDescriptor make_lifecycle_descriptor(std::string name, std::string summary, lifecycle_call call);
}
```

so the four public factories are one-liners that differ only in `name`, `summary` and the lambda:
`[&s](const std::string& slug){ return s.DeleteLink({slug}); }`, `SetLinkEnabled({slug, true})`,
`SetLinkEnabled({slug, false})`, `RestoreLink({slug})`. The handler returns
`link_view_result(call(slug))`.

## Implementation steps

1. Create the file with the local helper and the four factories.
2. Add declarations, sources and the test.
3. Build and run.

## Commands

```powershell
cmake --build cmake-build --target commands__24_link_lifecycle_commands
ctest --test-dir cmake-build -R "^commands__24_link_lifecycle_commands$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/24_link_lifecycle_commands.cpp` | `commands__24_link_lifecycle_commands` | unit | for each of the four commands: success returns text equal to `serializeLinkViewJson` of the stored link and the stored flag/`deleted_at` changed as expected; unknown slug -> `not_found`; missing `slug` -> `invalid_field`; extra key -> `invalid_field`; empty-string slug -> `not_found` (service semantics) or `invalid_field` (whichever the registry's string rule yields; assert one and document); `ControlSet::User` -> `not_authorized` and store unchanged; `dry_run:true` -> `unsupported` (registry step 5), store unchanged; forced store failure -> `storage_failure`; sequence delete -> restore -> delete is idempotent in shape; enable on an already-enabled link succeeds; disable then enable round trip; deleted link: `status` is `deleted` in the delete result. |

Manual check: none.

## Constraints

- C1/C3: bodies come from `serializeLinkViewJson` only; no per-command serializer.
- C7: permission `write` is declared on the descriptor and enforced by the registry, never inside the handler.
- C6: audit lines (registry) carry command, principal, outcome, never the slug value if treated as sensitive
  by 01.0/09; handler logs nothing.
- Mutating command rule: all four set `mutating = true` so the TUI confirms and the audit sink fires.
- C++17; `.cpp` in `COMMON_CPP_SOURCES`.
- ArgSpec string caps follow [07](07-link-create-command.md) (`max_length = 65536`) so the service remains the only validator.

## Success criteria

1. `commands__24_link_lifecycle_commands` passes.
2. A `ControlSet::User` call to each of the four commands leaves the fake store byte-identical (test compares before/after).
3. `dry_run:true` on each is `unsupported`.

## Out of scope / risks

- Hard delete (`IMetadataRepository::DeleteLink` physical removal) is deliberately not a command.
- `link.delete` not disabling the link means a soft-deleted link may still redirect on backends where
  `LinkService::Resolve` ignores `deleted_at` (it only checks `is_active` and `expires_at`, verified).
  This is a Task 02.0 mapping concern (record `is_active`/`deleted_at` encoding); raised here because the TUI
  will make deletion much more visible.
