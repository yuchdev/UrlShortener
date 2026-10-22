# 06 - Dry-run validation methods on LinkCommandService

**Parent task:** 03.0 Link and browse commands on the registry
**State:** ⬜ Not started
**Depends on:** none (changes only `app::LinkCommandService`; independent of 01.0)
**Blocks:** 07, 09, 05.0/08
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

The catalog marks `link.create` and `link.update` as supporting `options.dry_run`
("validate and authorize, do not mutate"). Today `LinkCommandService::CreateLink` and
`UpdateLink` validate and persist in one pass, so a handler cannot honor `dry_run`
without a service entry point that runs the same validation and **skips the write**.
Add `ValidateCreateLink` / `ValidateUpdateLink` as thin variants of the existing
methods, so validation is never duplicated.

## Dependencies

- Verified code to refactor (`src/app/link_command_service.cpp`): `CreateLink` normalizes the
  URL (`normalizeTargetUrl`), `chooseSlug` (validity, reserved, `store_.slugExists`),
  redirect type, expiry parse and default, tag/metadata/campaign validators, builds `Link`,
  `store_.create`, `store_.invalidateCache`, then `toView`. `UpdateLink` loads via
  `store_.findBySlug`, applies the PATCH fields with validators, sets `updated_at`, then `store_.update`.
- Existing tests that must stay green: `tests/unit/app/01..10,13..18` (target prefix `app__`).
- Fakes: `tests/unit/app/app_test_fakes.hpp` (`FakeLinkStore`, `FakeLinkStatsReader`, `makeTestConfig()`).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/app/link_command_service.hpp` | modify | Declare `ValidateCreateLink`, `ValidateUpdateLink`; add private `CreateLinkImpl`, `UpdateLinkImpl`. |
| `src/app/link_command_service.cpp` | modify | Move the bodies of `CreateLink`/`UpdateLink` into the `Impl` functions guarded by a `persist` flag; public methods delegate. |
| `tests/unit/app/40_link_command_service_validate_create.cpp` | create | Dry-run create behavior. |
| `tests/unit/app/41_link_command_service_validate_update.cpp` | create | Dry-run update behavior. |
| `sources.cmake` | modify | Append both to `APP_UNIT_SOURCES` (currently ends at `18_...`; Task 02.0 takes 20-2x, so this task uses 40+). No `CMakeLists.txt` change (the `app__` loop already exists). |

## Classes and functions

```cpp
// link_command_service.hpp, namespace url_shortener::app
/**
 * @brief Run every CreateLink check and return the view CreateLink would return,
 *        without calling ILinkStore::create or invalidateCache.
 * The returned id/created_at are freshly generated placeholders; a generated slug is
 * NOT reserved, so a later real create may choose a different one.
 */
Result<LinkView> ValidateCreateLink(const CreateLinkCommand& command) const;

/** @brief Run every UpdateLink check (including existence) without ILinkStore::update. */
Result<LinkView> ValidateUpdateLink(const UpdateLinkCommand& command) const;

private:
    Result<LinkView> CreateLinkImpl(const CreateLinkCommand& command, bool persist) const;
    Result<LinkView> UpdateLinkImpl(const UpdateLinkCommand& command, bool persist) const;
```

Error handling is unchanged: `Result<LinkView>` with `AppError{code, detail}`.
`ValidateUpdateLink` returns the *would-be* view (fields applied, `updated_at` refreshed in the view only).

## Implementation steps

1. Rename the current bodies to `CreateLinkImpl` / `UpdateLinkImpl(..., bool persist)`; wrap the
   `store_.create` + `store_.invalidateCache` block and the `store_.update` block in `if (persist)`.
2. Make `CreateLink(c)` return `CreateLinkImpl(c, true)`; add `ValidateCreateLink` -> `(c, false)`;
   same for update.
3. Add the two test files (they call the public methods with `FakeLinkStore`).
4. Run the whole `app__` suite to prove no behavior change.

## Commands

```powershell
cmake --build cmake-build --target app__40_link_command_service_validate_create app__41_link_command_service_validate_update
ctest --test-dir cmake-build -R "^app__" --output-on-failure
```

(`-R "^app__"` runs every app-layer test, including the two new ones; multi-config adds `-C Debug`.)

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/app/40_link_command_service_validate_create.cpp` | `app__40_link_command_service_validate_create` | unit | valid command -> ok view equal to what `CreateLink` returns on a second fresh store (compare all fields except `id`/timestamps); store untouched (`findBySlug` still empty); invalid URL, reserved slug, invalid slug, slug conflict, bad `expires_at`, tag/metadata/campaign violations return the same `AppErrorCode` as `CreateLink`; forced storage failure (`forceCreateStorageFailure`) does **not** surface (no write attempted). |
| `tests/unit/app/41_link_command_service_validate_update.cpp` | `app__41_link_command_service_validate_update` | unit | not found -> `not_found`; PATCH tri-state (absent / present / explicit null) results match `UpdateLink`; invalid field codes match; store content unchanged afterwards (compare via `findBySlug`); `forceUpdateStorageFailure` does not surface. |

## Constraints

- Zero behavior change for `CreateLink`/`UpdateLink` (existing tests are the proof; REST/CLI C3/C4).
- No validation logic copied: both variants share one implementation (milestone C1).
- Redirect fast path untouched (C10); `LinkService::Resolve` is not involved.
- C++17; Doxygen on the new public methods; `lower_case`/`m_` naming for new private members
  (existing members use trailing underscore, keep the file's local style).

## Success criteria

1. All existing `app__` tests plus the two new ones pass.
2. `git diff` of `link_command_service.cpp` contains no duplicated validation block (each validator call appears once).
3. A dry-run create followed by `findBySlug` returns `nullopt` (test 19).

## Out of scope / risks

- `link.import` dry-run (Task 05.0) builds on `ValidateCreateLink` per row; it is not specified here.
- Race: a real create after a successful dry-run may still fail with `slug_conflict`. Documented, expected.
- If 01.0/08 decides `dry_run` is implemented by the registry (never calling the handler), these methods
  are still needed for the command's *result*; keep them.
