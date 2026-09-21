# Task 01.0 - Command layer completion

**Parent milestone:** [Milestone 0003 - CLI & REST Command Interfaces](/docs/roadmap/0003-cli_rest_interfaces/plan.md)
**Status:** ⬜ Not started

## Scope

`app::LinkCommandService` (`include/url_shortener/app/link_command_service.hpp`,
`src/app/link_command_service.cpp`) currently implements `CreateLink`,
`GetLink`, and `GetLinkStats` only. `src/http/handlers/link_handlers.cpp`
implements the remaining six link-management operations
(`handlePatchLink`, `handleDeleteLink`, `handleLifecycleAction` for
enable/disable/restore, `handlePreviewLink`) inline, calling
`url_shortener::getLinkForRead()` / `url_shortener::updateLinkAndInvalidateCache()`
directly. This task adds the missing DTOs and `LinkCommandService` methods,
then migrates those six handlers onto them, with **zero change** to the
JSON/status code each currently returns.

This task is a pure refactor from the REST caller's point of view - it
exists so that Task 02.0/03.0 have a complete, transport-agnostic command
layer to build a CLI adapter against, not to change any HTTP behavior.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Define new command DTOs and service methods](01-define-update-delete-lifecycle-preview-dtos.md) | ⬜ Not started | 02, 03 |
| 02 | [Migrate patch and delete handlers](02-migrate-patch-and-delete-handlers.md) | ⬜ Not started | 04 |
| 03 | [Migrate lifecycle and preview handlers](03-migrate-lifecycle-and-preview-handlers.md) | ⬜ Not started | 04 |
| 04 | [Characterization tests for migrated handlers](04-characterization-tests-for-migrated-handlers.md) | ⬜ Not started | none |

## Key constraints

- No change to any REST response currently produced by `handlePatchLink`,
  `handleDeleteLink`, `handleLifecycleAction`, or `handlePreviewLink` (status
  code, JSON field names/order/values, error codes).
- `LinkCommandService` and its new DTOs stay free of Beast types - see C2 in
  `plan.md`.
- `LegacyLinkStore` (`include/url_shortener/app/legacy_adapters.hpp`) is
  extended, not replaced; it keeps wrapping the same `linkRepository()`
  singleton the inline handler code uses today, so behavior is provably
  unchanged.
- New public symbols carry Doxygen comments, per repository convention.
