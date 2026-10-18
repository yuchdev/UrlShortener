# 09 - link.import on_conflict=overwrite (service support)

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 05.0/08
**Blocks:** 20
**Estimated size:** S
**Suggested agent:** cpp-expert (design check: app-architect; security review: security-auditor)

## Objective

(Open decision D2 in the decision log: confirm this method, or drop `overwrite` from the enum.)

Make `on_conflict=overwrite` real. Today nothing in the app layer can change a link's `target_url`: `app::UpdateLinkCommand` has no such field (verified) and a soft-deleted link still occupies its slug, so "delete then create" cannot work. Add one narrow service method that replaces the target URL of an existing, non-deleted link using the same URL normalization and SSRF check as creation, and switch `link.import` to use it for slug conflicts when the policy is `overwrite`.

## Dependencies

- 05.0/08 (the import body and its policy switch).
- Verified: `LinkCommandService::UpdateLink` (PATCH, no target field), `ILinkStore::update(const Link&, AppError*)`, `ILinkStore::findBySlug`, `normalizeTargetUrl(url, ServerConfig)` in `core/utils.h`, `currentTimestamp()`.
- Design decision (see Out of scope): the new method is separate from `link.update` so the public `link.update` contract and ArgSpec (catalog 3.1) stay unchanged (C3).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/app/link_command_service.hpp` | modify | NEW `ReplaceLinkTargetCommand`, NEW `LinkCommandService::ReplaceLinkTarget` |
| `src/app/link_command_service.cpp` | modify | implementation |
| `src/commands/pair_commands.cpp` | modify | on `slug_conflict` with `overwrite`, call `ReplaceLinkTarget`; remove the "overwrite unsupported" guard except for REST |
| `tests/unit/app/60_link_command_service_replace_target.cpp` | create | service-level cases (Task 05.0 owns `tests/unit/app/60-69`, plan.md "Test file prefixes") |
| `tests/unit/commands/58_link_import_overwrite.cpp` | create | import-level cases |
| `sources.cmake` | modify | add both tests (`APP_UNIT_SOURCES`, `COMMANDS_UNIT_SOURCES`) |

## Classes and functions

```cpp
namespace url_shortener::app {
struct ReplaceLinkTargetCommand            // NEW
{
    std::string slug;        // existing link
    std::string target_url;  // new target, un-normalized
};

// LinkCommandService (NEW method)
Result<LinkView> ReplaceLinkTarget(const ReplaceLinkTargetCommand& command) const;
}
```

Behavior:

1. `normalizeTargetUrl(command.target_url, config_)` fails -> `invalid_url` (same message as `CreateLink`).
2. `store_.findBySlug(slug)`; missing -> `not_found`. A soft-deleted link (`deleted_at` set) -> `slug_conflict` ("slug belongs to a deleted link; restore it first"): overwrite never resurrects or silently edits a deleted record.
3. Set `target_url`, `updated_at = currentTimestamp()`; keep `id`, `created_at`, `enabled`, `tags`, `metadata`, `campaign`, `expires_at`, `redirect_type` unchanged.
4. `store_.update(link, &err)`; failure -> `storage_failure` (as `UpdateLink` does). `store_.invalidateCache(slug)` is already done inside `update` per `ILinkStore` contract; do not add a second call path.
5. Return `toView(link)`.

Import wiring: for a row whose `CreateLink` result is `slug_conflict` and policy is `overwrite`: the row must have an explicit slug (a generated slug cannot conflict), call `ReplaceLinkTarget`; success -> count it under a NEW additive result field `"overwritten":n` (not folded into `created`); failure -> `failed++` with the code. REST callers still get `unsupported` at factory time.

## Implementation steps

1. Add the command struct and method declaration with a Doxygen comment matching its siblings.
2. Implement it next to `UpdateLink`, reusing the private helpers (`toView`).
3. Add service tests first (they fail), then the implementation.
4. Update the import body switch; extend the result serializer with `overwritten` (additive, `v` unchanged, C2).
5. Write import-level tests; update the catalog row for `link.import` result in the 08.0 docs sync (note in the PR; do not edit `docs/commands/README.md` in this subtask beyond the task list in 08.0).
6. Re-run the whole `app__` and `commands__` suites.

## Commands

```text
cmake --build cmake-build --target app__60_link_command_service_replace_target commands__58_link_import_overwrite
ctest --test-dir cmake-build -R "^app__60_link_command_service_replace_target$" --output-on-failure
ctest --test-dir cmake-build -R "^commands__58_link_import_overwrite$" --output-on-failure
ctest --test-dir cmake-build -R "^app__" --output-on-failure
```
Windows/multi-config: `--config Debug` / `-C Debug`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/app/60_link_command_service_replace_target.cpp` | `app__60_link_command_service_replace_target` | unit | replaces target and bumps `updated_at`; other fields untouched; invalid URL -> `invalid_url`; private host -> `invalid_url` (and allowed when `shortener_allow_private_targets=true`); unknown slug -> `not_found`; soft-deleted slug -> `slug_conflict`, record unchanged; store `update` failure -> `storage_failure`; cache invalidated (fake records `invalidateCache`) |
| `tests/unit/commands/58_link_import_overwrite.cpp` | `commands__58_link_import_overwrite` | unit | import with `overwrite`: existing slug gets the new target and is counted `overwritten`; new slug still `created`; deleted slug -> `failed`/`slug_conflict`; row without slug never overwrites; dry-run reports `overwritten` count and changes nothing; REST caller -> `unsupported`; `skip` and `fail` behavior from 08 unchanged |

Manual check: import a CSV twice with `skip` then `overwrite` through the CLI and confirm `link get` shows the new target.

## Constraints

- C3: `link.update`, its ArgSpec and the REST PATCH body are not changed.
- Single validation path: URL normalization and SSRF check come from `normalizeTargetUrl` inside the service.
- C6/audit: overwrite is destructive; it is covered by the job's single audit line (mutating command) and a per-job counter, not per-row values.
- C7: still `write` permission only; no new permission.
- C++17; naming per `.clang-tidy`; `.cpp` already in `sources.cmake`; C10 untouched.

## Success criteria

1. Both new test targets pass; `ctest -R "^app__"` still green.
2. `git diff` on `src/http/` and `src/cli/` is empty (no adapter changed).
3. `link.import` with `overwrite` through stdio (18) produces the same counts as through the direct registry.

## Out of scope / risks

- Exposing `target_url` on `link.update` (would be a catalog change; propose separately).
- Overwriting metadata/tags/campaign from the pair row: only the target changes.
- Design problem to confirm with the owner: the catalog says `overwrite` exists but defines no semantics; the choices above (separate method, deleted slugs conflict, `overwritten` counter) are this spec's proposal. If rejected, cut `overwrite` from the `link.import` ArgSpec enum and delete this subtask.
