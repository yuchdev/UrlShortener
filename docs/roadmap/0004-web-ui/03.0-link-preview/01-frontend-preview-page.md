# 01 - Frontend Preview Page

**Parent task:** 03.0 Link Preview
**State:** ⬜ Not started
**Depends on:** 01.0 (shell, API client, preview schema), 02.0 subtask 02 (fault model)
**Blocks:** 02

## Objective

Build a read-only `/app/preview/{slug}` page that fetches the existing public
preview endpoint and renders the link's target URL, status, redirect type, and
active state, with explicit not-found / expired / disabled states.

## Files to add / edit

```text
web/app/src/pages/PreviewPage.tsx
web/app/src/components/preview/PreviewCard.tsx
web/app/src/hooks/usePreviewLink.ts
web/app/src/app/router.tsx          # edit: register /app/preview/$slug
web/app/src/api/linksApi.ts         # edit: finalize previewLink(slug) if stubbed
```

## Requirements

1. Register the `/app/preview/{slug}` route in TanStack Router and read the
   slug from the route params.
2. `usePreviewLink(slug)` is a TanStack Query query calling
   `GET /api/v1/links/{slug}/preview`, validated against the preview Zod
   schema (`{ slug, url, status, redirect_type, enabled, expires_at,
   deleted_at }`) from
   [01.0 subtask 03](/docs/roadmap/0004-web-ui/01.0-web-app-shell-static-hosting-shorten-flow/03-api-client-and-schemas.md).
3. `PreviewCard` renders: the target `url`, the `status`, the `redirect_type`,
   and a clear active/inactive indicator derived from
   `status`/`enabled`/`expires_at`/`deleted_at`. It states plainly whether the
   link will currently redirect.
4. Explicit states:
   - **not found** (`404`) → a clear "no such short link" message;
   - **expired** / **disabled** / **deleted** → a state derived from the
     preview payload (`enabled=false`, `expires_at` in the past, `deleted_at`
     set), shown as an honest "this link is not active" state, not an error;
   - loading → `Spinner`; transport failure → reuse the `network_error` /
     `timeout` fault states from task 02.0.
5. The displayed target URL is untrusted: render it as inert text or a link
   with `rel="noopener noreferrer nofollow"`; never auto-navigate or preload
   it.

## Constraints

- Read-only. No edit/disable/delete controls (see
  [plan.md SC6](/docs/roadmap/0004-web-ui/plan.md)).
- No new backend endpoint; consume the existing public preview endpoint.
- Reuse the fault-state model and UI primitives rather than inventing new
  error surfaces.

## Success criteria

- [ ] `/app/preview/{slug}` fetches and renders target URL, status, redirect
      type, and active state for an existing link.
- [ ] A missing slug renders a clear not-found state (not a generic error).
- [ ] Expired / disabled / deleted links render an honest "not active" state
      derived from the preview payload.
- [ ] The target URL is rendered safely (inert / `rel`-guarded), never
      auto-navigated.
- [ ] Loading and transport-failure states reuse the shared components/model.
