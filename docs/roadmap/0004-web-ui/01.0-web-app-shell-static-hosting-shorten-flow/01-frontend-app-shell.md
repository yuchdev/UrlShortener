# 01 - Frontend App Shell

**Parent task:** 01.0 Web App Shell, Static Hosting, and Shorten Flow
**State:** ⬜ Not started
**Depends on:** none
**Blocks:** 03, 04

## Objective

Scaffold the React + TypeScript + Vite application under `web/app/` with its
build tooling, provider tree, routing skeleton, base UI primitives, and layout
chrome — everything needed to render a shell page under `/app`, before any API
or form logic exists.

## Files to add

```text
web/app/package.json
web/app/tsconfig.json
web/app/vite.config.ts
web/app/index.html
web/app/src/main.tsx
web/app/src/app/App.tsx
web/app/src/app/router.tsx
web/app/src/app/providers.tsx
web/app/src/app/config.ts
web/app/src/components/layout/AppLayout.tsx
web/app/src/components/layout/Header.tsx
web/app/src/components/layout/Footer.tsx
web/app/src/components/ui/Button.tsx
web/app/src/components/ui/Card.tsx
web/app/src/components/ui/Input.tsx
web/app/src/components/ui/Spinner.tsx
web/app/src/components/ui/EmptyState.tsx
web/app/src/components/ui/ErrorState.tsx
web/app/src/pages/ShortenPage.tsx
web/app/src/pages/NotFoundPage.tsx
```

## Requirements

1. Use React + TypeScript + Vite, matching the stack in
   [plan.md - Architecture](/docs/roadmap/0004-web-ui/plan.md).
2. Configure Vite with `base: "/app/"` so built asset URLs resolve under the
   `/app` prefix served by the backend (see
   [subtask 02](02-backend-static-hosting.md) and
   [plan.md SC4](/docs/roadmap/0004-web-ui/plan.md)).
3. Wire TanStack Router in `app/router.tsx` with a `/app` root; register at
   minimum `/app` (the shorten page) and a catch-all `NotFoundPage`. The
   `/app/preview/{slug}` route is added by task 03.0.
4. Wire TanStack Query in `app/providers.tsx` with sensible defaults
   (mutations for create; short/zero stale-time — this app has no long-lived
   cached lists).
5. `Header` and `Footer` render minimal branding/chrome; `AppLayout` wraps a
   routed page outlet. No navigation menu is required — this is a
   single-purpose tool.
6. `Button`, `Card`, `Input`, `Spinner`, `EmptyState`, `ErrorState` are
   Tailwind-based internal component wrappers (shadcn/ui-style). Pages must
   not use a third-party component library directly.
7. `ShortenPage` here is a placeholder shell (e.g. a `Card` with a static
   heading) — the real form lands in [subtask 04](04-shorten-flow-ui.md).
8. `web/app/package.json` must be buildable with `npm ci && npm run build`
   producing `web/app/dist/`.

## Constraints

- Layout / scaffolding only — no API client, no form logic, no Zod schemas in
  this subtask's files (those are subtasks 03 and 04).
- No auth, no `localStorage`, no cookies — public page.
- Keep the primitive set minimal; do not add speculative UI components not
  used by the shorten or preview flows.

## Success criteria

- [ ] `cd web/app && npm ci && npm run build` succeeds and produces
      `web/app/dist/` with assets referencing the `/app/` base.
- [ ] The app renders `AppLayout` (`Header` + `Footer`) around a routed page
      outlet.
- [ ] `/app` renders the `ShortenPage` placeholder; an unknown in-app route
      renders `NotFoundPage`.
- [ ] The six UI primitives exist as Tailwind wrappers and are exported for
      reuse.
- [ ] No API, auth, or form logic exists in this subtask's files.
