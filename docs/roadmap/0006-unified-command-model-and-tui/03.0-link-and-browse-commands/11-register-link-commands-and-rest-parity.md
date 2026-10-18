# 11 - Register the nine link commands and the REST parity gate

**Parent task:** 03.0 Link and browse commands on the registry
**State:** ⬜ Not started
**Depends on:** 03.0/07, 03.0/08, 03.0/09, 03.0/10; 02.0/07 (`LinkBackend` holder: REST and registry must obtain the same `ILinkStore`)
**Blocks:** 14, 18, 04.0/03, 04.0/04, 04.0/05, 04.0/14, 06.0/12, 06.0/13 (all REST/CLI re-pointing and the TUI link screens)
**Estimated size:** M
**Suggested agent:** testing-expert (parity test), cpp-expert (registration function)

## Objective

Provide the single registration entry point for the nine link commands and prove, before
any transport is touched, that the registry produces **exactly** the bodies and error
classes the current REST handlers produce. This test is the gate that lets Task 04.0 swap
the REST handler bodies without risking contract C3.

## Dependencies

- Subtasks 07-10 (the factories) and 01.0/06-07 (`CommandRegistry::add`, `execute`).
- Same store for REST and registry. Today REST builds `app::BuildLegacyLinkCommandService(config)` per request
  (`makeCommandService` in `link_handlers.cpp`; global `linkRepository()` singleton). After 02.0 the handlers
  obtain the store from `app::LinkBackend` (02.0 README decision 1; 02.0 subtask 09). The test must build its
  `LinkCommandService` from the same source the handlers use - one factory call to adapt (**UNVERIFIED** final name).
- Verified REST harness pattern: `tests/unit/http/10_link_handlers.cpp` dispatches
  `RouterBuilder::buildApplicationRouter().dispatch(req, config, false)` and seeds via
  `url_shortener::linkRepository().create(link)`.
- Error table: `error_info_for(code).legacy_rest_status` / `.legacy_rest_code` (01.0/02).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/link_commands.hpp` | modify | Declare `register_link_commands`. |
| `src/commands/link_commands.cpp` | create | Calls the nine factories and `registry.add`. Add to `COMMON_CPP_SOURCES`. |
| `tests/unit/commands/25_link_commands_rest_parity.cpp` | create | Parity corpus. |
| `sources.cmake` | modify | Standard additions. |

## Classes and functions

```cpp
namespace url_shortener::commands {
/// Registers link.create/get/update/delete/enable/disable/restore/preview/stats.
/// @p service must outlive @p registry. Adds exactly nine descriptors.
void register_link_commands(CommandRegistry& registry, app::LinkCommandService& service);
}
```

Test helpers (file-local):

```cpp
struct normalized { std::string text; };
// Replaces the slug text with "<slug>", RFC 3339 timestamps with "<ts>", the "id" value with "<id>"
// (ids and timestamps are generated), so two runs on different slugs compare equal.
std::string normalize(std::string body, const std::string& slug);
```

## Implementation steps

1. Implement `register_link_commands` (nine `registry.add(make_...)` calls, order = catalog order).
2. Write the parity test. For each operation, run it once through the REST router on slug `par-rest-N`
   and once through `registry.execute` on slug `par-cmd-N` against identically seeded links (distinct
   slugs because the singleton store has no delete), then compare `normalize(rest_body, slug)` with
   `normalize(result_json, slug)`.
3. For failures compare classes, not text: REST `status` equals `error_info_for(code).legacy_rest_status`
   and REST `"code"` equals `legacy_rest_code`, for the cases where the failure comes from the service
   (not from REST per-field validation).
4. Run the new test plus the REST characterization tests.

## Commands

```powershell
cmake --build cmake-build --target commands__25_link_commands_rest_parity 10_link_handlers
ctest --test-dir cmake-build -R "^(commands__25_link_commands_rest_parity|10_link_handlers|02_endpoint_matrix_characterization)$" --output-on-failure
```

(`10_link_handlers` and the other `HTTP_UNIT_SOURCES` tests are plain executables named by file stem.)

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/25_link_commands_rest_parity.cpp` | `commands__25_link_commands_rest_parity` | unit | success parity for create (full args incl. tags/metadata/campaign), get by slug, get by id, update (value, `null` clear, absent), delete, enable, disable, restore, preview (enabled/disabled/deleted/expired), stats (range mode via seeded `analyticsClickRepository()` events, and lifetime mode); failure-class parity: get unknown (404 `not_found`), create invalid URL (400 `invalid_url`), reserved slug (409 `reserved_slug`), duplicate slug (409 `slug_conflict`), update unknown (404), stats invalid bucket; `registry.list()` contains exactly the nine names; each descriptor's `mutating`/`permission` equals the catalog (write for mutations, read for get/preview/stats). |

Manual check: none; failures print both normalized bodies.

## Constraints

- C3: this test must never be loosened to "similar"; normalization only masks generated values.
- The test must not modify `link_handlers.cpp`; it observes it. Task 04.0 will keep this test green while
  deleting the handlers' DTO code.
- Link test state is global (`linkRepository()` singleton): use unique slugs per case (the CLI tests
  document the same hazard).
- C++17; naming per `.clang-tidy`; the test links `url_shortener_common` like all `commands__` tests.

## Success criteria

1. `commands__25_link_commands_rest_parity` passes; the REST characterization tests are untouched and green.
2. Temporarily changing one field name in `serializeLinkViewJson` makes both REST and the parity test change together
   (they share the serializer) - and temporarily editing a handler's decode logic fails the parity test.
3. `registry.list().size()` is 9 after `register_link_commands` on an empty registry (plus `command.list` if 01.0 pre-registers it; assert by name set).

## Out of scope / risks

- Quirks that the parity test will expose and that need an explicit decision rather than a silent fix:
  (a) REST per-field 400 codes happen before the service and have no command equivalent;
  (b) REST `PATCH` checks existence before body validation; (c) `reserved_slug` is HTTP 409 on REST while the
  catalog says 400. These stay REST-adapter concerns (Task 04.0) and are listed there.
- Jobs and `link.list` are not registered here (`link.list`: subtask 14; jobs: Task 05.0).
