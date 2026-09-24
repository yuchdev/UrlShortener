# 02 - Preview Tests

**Parent task:** 03.0 Link Preview
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** none

## Objective

Cover the preview page with Vitest component tests against MSW, including the
active, not-found, expired, and disabled states.

## Files to add

```text
web/app/src/pages/PreviewPage.test.tsx
web/app/src/test/fixtures/previewView.ts
```

Plus edits to the shared MSW handlers to add preview responses.

## Requirements

1. Add MSW handlers for `GET /api/v1/links/{slug}/preview` returning: an
   active link, a disabled link (`enabled=false`), an expired link
   (`expires_at` in the past), a deleted link (`deleted_at` set), and a `404`
   not-found.
2. Assert the rendered `PreviewCard` for the active case shows target URL,
   status, redirect type, and an active indicator.
3. Assert each inactive case (disabled / expired / deleted) renders the honest
   "not active" state, distinct from the not-found state.
4. Assert the `404` renders the not-found state.
5. Assert the target URL is rendered inertly / with the guarded `rel` and is
   not auto-navigated.

## Constraints

- MSW only; no real backend.
- Reuse the fixtures and handler structure from tasks 01.0/02.0 so task 04.0
  can consolidate them.

## Success criteria

- [ ] Active, disabled, expired, deleted, and not-found preview states are
      each asserted via MSW.
- [ ] The inactive states are visibly distinct from the not-found state.
- [ ] The target-URL safe-rendering behavior is asserted.
- [ ] `cd web/app && npm run test` is green.
