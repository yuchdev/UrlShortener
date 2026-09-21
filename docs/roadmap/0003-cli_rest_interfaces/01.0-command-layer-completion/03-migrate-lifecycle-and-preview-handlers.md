# 03 - Migrate lifecycle and preview handlers

**Parent task:** 01.0 Command layer completion
**State:** ✅ Complete
**Depends on:** 01
**Blocks:** 04

## Objective

Replace `handleLifecycleAction`'s three call sites
(`handleEnableLink`/`handleDisableLink`/`handleRestoreLink`) and
`handlePreviewLink` in `src/http/handlers/link_handlers.cpp` with calls to
`LinkCommandService::SetLinkEnabled`/`RestoreLink`/`PreviewLink`, preserving
every currently observable response.

## Files to modify

- `src/http/handlers/link_handlers.cpp`:
  - `handleLifecycleAction` - branch on `action` to build either
    `app::SetLinkEnabledCommand{slug, enabled=true|false}` (for
    `"enable"`/`"disable"`) or `app::RestoreLinkCommand{slug}` (for
    `"restore"`), call the corresponding `LinkCommandService` method, map
    the result via `appErrorResponse`.
  - `handlePreviewLink` - call `LinkCommandService::PreviewLink`, and either
    reuse its existing inline JSON body construction against the returned
    `LinkView`/`app::AppError`, or add a small preview-specific serializer if
    the field set genuinely differs from `LinkView` (slug/url/status/
    redirect_type/enabled/expires_at/deleted_at - a subset of `LinkView`'s
    fields, so a projection rather than a new struct is likely sufficient).

## Tests

- Extend/confirm `tests/unit/http/10_link_handlers.cpp` covers
  enable/disable/restore/preview response shapes before migrating, exactly
  as subtask 02 requires for patch/delete.

## Constraints

- `handleLifecycleAction`'s not-found handling (`linkNotFound`) must map
  from the same `AppErrorCode::not_found` path `LinkCommandService` already
  uses for `GetLink`, not a separate ad hoc check.
- Preview must remain a read-only operation - `PreviewLink` must not call
  any write path (`update`/`invalidateCache`).

## Success criteria

- [ ] `handleEnableLink`, `handleDisableLink`, `handleRestoreLink`, and
      `handlePreviewLink` call `LinkCommandService` exclusively.
- [ ] `handleLifecycleAction`'s three-way branch collapses to two
      `LinkCommandService` calls (`SetLinkEnabled` for enable/disable,
      `RestoreLink` for restore) rather than three separate inline mutation
      paths.
- [ ] `tests/unit/http/10_link_handlers.cpp` passes unchanged (or extended
      before migration, still green after).
