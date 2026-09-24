# Task 01.0 - Web App Shell, Static Hosting, and Shorten Flow

**Parent milestone:** [Milestone 0004 - Public Web UI](/docs/roadmap/0004-web-ui/plan.md)
**Status:** ⬜ Not started

## Scope

Deliver the MVP: a public, unauthenticated `/app` page where a user pastes a
long URL (optionally with a custom slug), submits, and gets back a short link
with a copy-to-clipboard button. This task stands up everything the later
tasks build on — the React scaffold, the backend static hosting for `/app`,
the API client + Zod schemas against the verified `/api/v1/links` shapes, and
the shorten flow UI itself — plus happy-path component tests.

This task establishes the cross-cutting pieces every later task depends on:
the app scaffold (Vite / TanStack Router / TanStack Query providers, Tailwind,
base UI primitives), the static-assets handler and its router-ordering, and
the typed API client. Fault-mode handling beyond a minimal error surface is
task 02.0; the preview page is task 03.0; consolidated test infra and e2e are
task 04.0.

See [plan.md SC1](/docs/roadmap/0004-web-ui/plan.md) for the backend API
contract this task consumes and [plan.md SC4](/docs/roadmap/0004-web-ui/plan.md)
for the static-hosting/router-ordering contract it must implement.

## Subtasks

| # | Document | Status | Depends on | Blocks |
|---|----------|--------|------------|--------|
| 01 | [Frontend app shell](01-frontend-app-shell.md) | ⬜ Not started | none | 03, 04 |
| 02 | [Backend static hosting](02-backend-static-hosting.md) | ⬜ Not started | 01 | none |
| 03 | [API client and Zod schemas](03-api-client-and-schemas.md) | ⬜ Not started | 01 | 04 |
| 04 | [Shorten flow UI](04-shorten-flow-ui.md) | ⬜ Not started | 03 | 05 |
| 05 | [Happy-path component tests](05-happy-path-component-tests.md) | ⬜ Not started | 04 | none |

## Key constraints

- No auth, no session, no credential storage of any kind — this is a public
  page (see [plan.md SC6](/docs/roadmap/0004-web-ui/plan.md)).
- The backend static-assets handler must be mounted **ahead of** the redirect
  and generic-fallback catch-alls and must never shadow `/{slug}`, `/api/*`,
  or observability routes (see [plan.md SC4](/docs/roadmap/0004-web-ui/plan.md)).
  This is new backend work — a small handler in `src/http/handlers/` — handed
  to `cpp-expert`; it is not a frontend concern.
- The frontend consumes only the existing public `/api/v1/links` endpoints;
  it creates no new backend read endpoint (see
  [plan.md SC1](/docs/roadmap/0004-web-ui/plan.md)).
- Response shapes are pinned with Zod; the schemas are the single point of
  update if the backend shape changes.
- Client-side URL validation may catch obviously-malformed input early but
  must never claim to know *why* the server rejected a plausible URL (see
  [plan.md SC3](/docs/roadmap/0004-web-ui/plan.md)) — full fault handling is
  task 02.0.
- `cd web/app && npm ci && npm run build` must succeed and produce
  `web/app/dist/` by the end of this task.
