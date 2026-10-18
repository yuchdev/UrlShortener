# 06 - REST delete and lifecycle routes through the registry, and handler cleanup

**Parent task:** 04.0 REST and CLI adapters
**State:** ⬜ Not started
**Depends on:** 02 (bridge), 01 (args builder), 03 (reads), 04 (create), 05 (patch) - the cleanup step removes helpers those subtasks stop using; 03.0/10 (lifecycle commands)
**Blocks:** 07, 09
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Move the last mutating resource routes - `DELETE /api/v1/links/{slug}` and `POST /api/v1/links/{slug}/enable|disable|restore` - onto
`link.delete`, `link.enable`, `link.disable`, `link.restore`, then delete the now-dead per-handler mapping code
(`makeCommandService`, `statusForAppError`, `codeForAppError`, `appErrorResponse`) so the milestone claim "handlers contain no
DTO construction or error mapping" becomes mechanically true.

## Dependencies

- Subtasks 01-05; 03.0/10 (slug-only lifecycle commands).
- Verified behavior (`handleDeleteLink`, `handleLifecycleAction` in `link_handlers.cpp`): all four answer `200` with
  `serializeLinkViewJson` of the updated link; unknown slug -> `404 not_found` "Link not found"; failed write -> `500 repository`;
  no request body is read.
- After subtasks 03-05, the old mapping helpers have no callers except these handlers and `handlePlaceholderFeature`
  (which only uses `linkNotFound`). Verify with grep before deleting.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `src/http/handlers/link_handlers.cpp` | modify | Rewrite `handleDeleteLink` and `handleLifecycleAction`; delete `makeCommandService`, `statusForAppError`, `codeForAppError`, `appErrorResponse` and unused includes (`link_command_service_factory.hpp`). Keep `linkNotFound`, `getQueryParam`, `extractCampaign`, `pathValue`. |
| `tests/unit/http/19_rest_lifecycle_via_registry.cpp` | create | Characterization + edge cases (written first). |
| `sources.cmake` | modify | `HTTP_UNIT_SOURCES` += the test. |

## Classes and functions

No new public symbols. Mapping:

| Route | Command | `args` |
|---|---|---|
| `DELETE /api/v1/links/{slug}` | `link.delete` | `{"slug"}` |
| `POST /api/v1/links/{slug}/enable` | `link.enable` | `{"slug"}` |
| `POST /api/v1/links/{slug}/disable` | `link.disable` | `{"slug"}` |
| `POST /api/v1/links/{slug}/restore` | `link.restore` | `{"slug"}` |

`handleLifecycleAction(req, config, is_tls, context, action)` keeps its `action` string parameter and maps it to the command name
(`"enable"` -> `"link.enable"` ...), then `resource_response(..., 200)`.

## Implementation steps

1. **Characterize first:** write `19_rest_lifecycle_via_registry.cpp` against the old code; green.
2. Rewrite the two handlers; run the tests.
3. `grep` for remaining callers of the four helpers; delete them and the include; rebuild everything that includes `link_handlers.hpp`.
4. Run the complete HTTP unit set (`HTTP_UNIT_SOURCES`) and the app-layer tests.

## Commands

```powershell
cmake --build cmake-build --target 19_rest_lifecycle_via_registry 10_link_handlers 11_compatibility_handlers
ctest --test-dir cmake-build -R "^(19_rest_lifecycle_via_registry|10_link_handlers|11_compatibility_handlers)$" --output-on-failure
cmake --build cmake-build
ctest --test-dir cmake-build -L unit --output-on-failure
```

(Full `unit` label run after the cleanup: the deleted helpers were file-local, but a full build proves nothing else referenced them.)

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/http/19_rest_lifecycle_via_registry.cpp` | `19_rest_lifecycle_via_registry` | unit | delete -> 200, body `status` is `deleted` and `deleted_at` set; delete twice -> 200 both times; delete unknown -> 404; enable/disable round trip (`status` `active`/`disabled`, `enabled` flag); restore after delete clears `deleted_at`; restore of a never-deleted link -> 200 unchanged status; enable on a deleted link -> 200 with `status` still `deleted`; each unknown slug -> 404 `not_found` "Link not found"; wrong method (`GET .../enable`) -> unchanged 404/405 behavior from `03_method_and_error_characterization`; request body ignored (garbage body still works); `X-Request-Id` echoed; redirect of a disabled/deleted link unaffected (re-run of `12_prefixed_redirect_handler`/`13_root_redirect_handler`). |

Manual check: disable a link with `curl`, then `curl -i localhost:8000/<slug>` still returns the pre-existing disabled-redirect response.

## Constraints

- C3: bodies/statuses unchanged; C10: redirect handlers are not edited (only re-run).
- C1: after this subtask `link_handlers.cpp` contains no `app::` command DTO, no `LinkCommandService`, no `AppErrorCode` switch.
- Do not delete `LegacyLinkStore` / `BuildLegacyLinkCommandService` here: the CLI still uses them until subtask 15, and Task 02.0 owns the backend factory.
- C++17; naming per `.clang-tidy`.

## Success criteria

1. `19_rest_lifecycle_via_registry` and every `HTTP_UNIT_SOURCES` test pass.
2. `grep -n "AppErrorCode\|LinkCommandService\|statusForAppError\|codeForAppError\|makeCommandService" src/http/handlers/link_handlers.cpp` is empty.
3. `git diff --stat` for this subtask shows only `link_handlers.cpp`, the new test and `sources.cmake`.

## Out of scope / risks

- Hard-delete, bulk operations, and `PUT` are not routes today and are not added.
- If another file still includes the removed helpers (none expected; they were in an anonymous namespace), the compile error is the check.
