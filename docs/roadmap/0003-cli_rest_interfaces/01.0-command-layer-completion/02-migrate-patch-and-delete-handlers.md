# 02 - Migrate patch and delete handlers

**Parent task:** 01.0 Command layer completion
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** 04

## Objective

Replace the inline bodies of `handlePatchLink` and `handleDeleteLink` in
`src/http/handlers/link_handlers.cpp` with calls to
`LinkCommandService::UpdateLink`/`DeleteLink`, keeping every currently
observable response identical (status code, JSON body via
`serializeLink`/`app::serializeLinkViewJson` - pick whichever the migrated
handler ends up using and confirm field-for-field parity, since
`serializeLink` and `app::serializeLinkViewJson` are not necessarily
identical today).

## Files to modify

- `src/http/handlers/link_handlers.cpp`:
  - `handlePatchLink` - parse the same JSON fields it does today
    (`enabled`, `expires_at`, `tags`, `metadata`, `campaign`,
    `deleted_at`-rejection) into `app::UpdateLinkCommand`, call
    `UpdateLink`, map the `Result` via the existing
    `appErrorResponse`/`statusForAppError`/`codeForAppError` helpers instead
    of the ad hoc `makeApiErrorResponse` calls currently inline.
  - `handleDeleteLink` - build `app::DeleteLinkCommand{slug}`, call
    `DeleteLink`, map the result the same way.

## Tests

- Extend/confirm `tests/unit/http/10_link_handlers.cpp` still passes
  unchanged (it already characterizes patch/delete responses) - if it does
  not yet cover every field combination `handlePatchLink` supports, add
  cases there first, then migrate, so the test would fail on a behavior
  change.
- Do not delete or weaken any existing case; this subtask must be provably
  regression-free, not just "still compiles."

## Constraints

- Serialize the migrated response the same way the pre-migration handler
  did; if the pre-migration handler used the local `serializeLink()` helper
  and it differs in any field from `app::serializeLinkViewJson`, either fix
  `serializeLinkViewJson` to match (preferred, since it is the shared
  serializer used by create/get) or explicitly document the deliberate
  divergence with a code comment before shipping - do not silently change
  the wire format.
- `handlePatchLink`'s three-state field semantics (absent / present-null /
  present-value) must survive the migration exactly, per subtask 01's DTO
  note.

## Success criteria

- [ ] `handlePatchLink` and `handleDeleteLink` call `LinkCommandService`
      exclusively; no direct `getLinkForRead`/`updateLinkAndInvalidateCache`
      calls remain in either function.
- [ ] `tests/unit/http/10_link_handlers.cpp` passes unchanged (or extended
      before migration, still green after).
- [ ] No response field, status code, or error code changed for any case
      already covered by existing tests.
