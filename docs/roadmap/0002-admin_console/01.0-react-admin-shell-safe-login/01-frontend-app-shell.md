# 01 - Frontend App Shell

**Parent task:** 01.0 React Admin Shell and Safe Login Integration
**State:** ⬜ Not started
**Depends on:** none
**Blocks:** 03

## Objective

Scaffold the React + TypeScript + Vite application under `web/admin/` with
its build tooling, provider tree, routing skeleton, base UI primitives, and
layout chrome - everything needed to render a shell page, before any auth
wiring exists.

## Files to add

```text
web/admin/package.json
web/admin/tsconfig.json
web/admin/vite.config.ts
web/admin/index.html
web/admin/src/main.tsx
web/admin/src/app/App.tsx
web/admin/src/app/router.tsx
web/admin/src/app/providers.tsx
web/admin/src/app/config.ts
web/admin/src/components/layout/AdminLayout.tsx
web/admin/src/components/layout/Sidebar.tsx
web/admin/src/components/layout/TopBar.tsx
web/admin/src/components/ui/Button.tsx
web/admin/src/components/ui/Card.tsx
web/admin/src/components/ui/Input.tsx
web/admin/src/components/ui/Spinner.tsx
web/admin/src/components/ui/EmptyState.tsx
web/admin/src/components/ui/ErrorState.tsx
web/admin/src/pages/DashboardPage.tsx
web/admin/src/pages/SettingsPage.tsx
```

## Requirements

1. Use React + TypeScript + Vite (per
   [00-decision-record.md](/docs/roadmap/0002-admin_console/00-decision-record.md)).
2. Wire TanStack Router (`app/router.tsx`, `app/routes.tsx`) with the route
   table from
   [02-react-frontend-architecture.md §2](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md) -
   at minimum register `/admin/login`, `/admin/dashboard`, and
   `/admin/settings` in this subtask; later tasks add their own routes.
3. Wire TanStack Query in `app/providers.tsx` with the stale-time defaults
   documented in
   [02-react-frontend-architecture.md §5](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md).
4. `Sidebar` must render the full main navigation structure from
   [01-product-and-ux-spec.md §2](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)
   (items for pages that do not exist yet may link to placeholder routes -
   visibility gating by role happens in subtask 03).
5. `TopBar` must render: global time range selector, timezone selector
   (Local/UTC), environment label, logged-in username, role badge, logout
   button - the username/role/logout wiring is completed in subtask 03; this
   subtask lays out the static structure.
6. `Button`, `Card`, `Input`, `Spinner`, `EmptyState`, `ErrorState` are
   Tailwind-based internal component wrappers (do not use a third-party
   component library directly in pages - see
   [00-decision-record.md §2](/docs/roadmap/0002-admin_console/00-decision-record.md)).
7. `web/admin/package.json` must be buildable with `npm ci && npm run build`
   producing `web/admin/dist/` (final `build`/`dev`/`preview` script wiring
   is finalized in task 09.0, but `npm run build` must already work after
   this subtask).

## Constraints

- Keep chart-library-specific and API-specific code out of this subtask -
  it is layout/scaffolding only.
- `DashboardPage` and `SettingsPage` here are placeholder shells (e.g. a
  `Card` with "Coming soon") - real content lands in tasks 03.0 and 08.0.

## Success criteria

- [ ] `cd web/admin && npm ci && npm run build` succeeds.
- [ ] The app renders `AdminLayout` with `Sidebar` and `TopBar` around a
      routed page outlet.
- [ ] `Sidebar` lists every top-level nav item from the product/UX spec.
- [ ] `TopBar` renders all required elements (time range, timezone,
      environment, username placeholder, role badge placeholder, logout
      button).
- [ ] No auth or API-fetching logic exists yet in this subtask's files.
