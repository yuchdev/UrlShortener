# Milestone 0004 - Public Web UI

## Background

The URL shortener today has no human-facing surface for its core action. A
user who wants a short link must call `POST /api/v1/links` directly (curl, a
script, an API client). This milestone adds the obvious missing thing: a
small, public, unauthenticated web page where anyone can paste a long URL,
optionally pick a custom slug, and get back a short link they can copy.

This is deliberately the **thin** counterpart to
[Milestone 0002 - Admin Console](/docs/roadmap/0002-admin_console/plan.md).
Where 0002 is an internal, login-gated, analytics-first operator console at
`/admin`, this milestone is a public, credential-free "shorten a URL" tool at
`/app`. The two share a frontend stack and a static-hosting pattern but share
no data, no auth, and no analytics surface. Nothing here reads or exposes
visitor identities, fingerprints, IPs, or click analytics.

The V1 scope is intentionally minimal: shorten a URL, show the result, copy
it, and optionally preview an existing slug. There are **no accounts, no link
history, no ownership, and no analytics charts** (see Non-goals below). The
entire backend dependency is the *already-public* `/api/v1/links` API — this
milestone adds exactly one new backend surface (static asset hosting for the
SPA) plus one small optional one (public-create rate limiting), and otherwise
reuses what exists.

Two facts about the existing backend shape the whole UX and are treated as
first-class design inputs rather than incidental details:

- **SSRF rejection is opaque to the client.** `normalizeTargetUrl`
  (`src/core/utils.cpp`) rejects private-host targets (`isPrivateHost`, unless
  `SHORTENER_ALLOW_PRIVATE_TARGETS`) with the *same* `invalid_url` 400 as a
  plainly malformed URL. The UI cannot and must not try to distinguish
  "malformed" from "blocked target" — see Shared contract SC3.
- **There is no rate limiting on link creation today.** `IRateLimiter` is
  wired only into the redirect path, not into `POST /api/v1/links`. A public,
  unauthenticated create form is an abuse vector, so a `429` fault mode
  requires genuinely new backend work — see Shared contract SC5.

Rationale for the prefix choice, the "no accounts in V1" line, and the
reuse-not-fork API decision lives in
[00-decision-record.md](/docs/roadmap/0004-web-ui/00-decision-record.md).

## Decision Record

Short, focused rationale for the three decisions that are not obvious from
[plan.md](/docs/roadmap/0004-web-ui/plan.md). Everything else that would live
in a decision record for a larger milestone is folded into plan.md's
Background instead — this UI is deliberately thin.

### 1. Why `/app` as the prefix, and the router-ordering constraint

The public UI needs a stable path that does not collide with anything the
server already routes. The taken/reserved space is:

- `/admin` — reserved by [Milestone 0002](/docs/roadmap/0002-admin_console/plan.md).
- `/{slug}` and `/r/{slug}` — the protected redirect fast path.
- `/api/*` and the observability routes — the management/ops plane.

`/app` is free, short, and reads naturally as "the app". In
`src/http/router.cpp` the `Router` dispatches by specificity
(`pathSpecificity` counts literal segments), so a literal `/app` already
outranks the `/{slug}` catch-all — a request for `/app` will not be
mistaken for a redirect slug.

The constraint is that this same router supports only a *single-segment*
catch-all and cannot express nested SPA asset paths like
`/app/assets/index-abc123.js`. So `/app` cannot be served by an ordinary
route registration alone. It needs a **dedicated static-assets handler
mounted ahead of the redirect and generic-fallback catch-alls**, matching the
pattern 0002 documents for `/admin`. That handler owns `/app` and everything
under `/app/*`, serving `index.html` for the SPA shell and unknown sub-paths
(client-side routing fallback) and hashed assets from `web/app/dist/`. Full
contract: [plan.md SC4](/docs/roadmap/0004-web-ui/plan.md).

### 2. Why no accounts, history, or ownership in V1

The product goal is the single most common action — turn a long URL into a
short one — available to anyone with zero friction. Accounts, "my links",
ownership, and editing all imply identity, persistence of *who*, and an auth
surface. That is a different, heavier product, and the identity/analytics
half of it already has a home in 0002.

Keeping V1 anonymous also keeps the attack surface small: no credentials to
leak, no session to fix, no per-user data to mask. The one abuse vector an
anonymous public create form does introduce — unbounded automated creation —
is addressed narrowly by the new rate-limiting surface
([plan.md SC5](/docs/roadmap/0004-web-ui/plan.md)) rather than by gating the
whole tool behind login. The full non-goals list is
[plan.md SC6](/docs/roadmap/0004-web-ui/plan.md).

### 3. Why reuse, not fork, the existing `/api/v1/links` API

`POST /api/v1/links`, `GET /api/v1/links/{slug}/preview`, and
`GET /api/v1/links/{slug}` already exist, already require no auth, and already
return well-defined JSON (see `link_handlers.cpp` and
`src/app/link_command_service.cpp`). A public UI that shortens and previews
links needs nothing more. Forking a parallel "public" API would duplicate
validation, SSRF checks, slug generation, and the error envelope, and would
create two code paths that must be kept in agreement.

So the UI consumes those endpoints directly and treats their response shapes
as the contract, pinned by Zod schemas on the frontend
([plan.md SC1](/docs/roadmap/0004-web-ui/plan.md)). The only new backend work
this milestone adds is (a) the static-assets handler for `/app` and (b) the
optional public-create rate limiter — both additive, neither forking the link
API.

## Architecture

| Area | Decision |
|---|---|
| Frontend framework | React + TypeScript |
| Build tool | Vite |
| Routing | TanStack Router |
| Server state | TanStack Query |
| Forms | React Hook Form + Zod |
| Styling | Tailwind CSS + shadcn/ui-style internal component wrappers |
| Auth model | None — public and unauthenticated by design |
| Backend dependency | The existing public `/api/v1/links` API only |
| Static hosting | New C++ static-assets handler serving `/app` and `/app/*`, mounted ahead of the redirect/fallback catch-alls |
| New backend surface | Config-gated, fail-open public-create rate limiter (returns `429` in the standard error envelope) |
| Component tests | Vitest + MSW | 
| E2E tests | Playwright |

The frontend lives under `web/app/` (see the File map below), and is served
as static assets by the existing C++ backend under `/app`, following the same
static-SPA-under-a-path-prefix pattern that 0002 documents for `/admin` (task
[0002 01.0](/docs/roadmap/0002-admin_console/01.0-react-admin-shell-safe-login/README.md)).
0002 is itself fully specified but unimplemented, so this milestone borrows
its *pattern*, not its code. The only new backend module is a small
static-assets handler and (in task 02.0) an optional rate-limiting check in
the create path; all link creation and preview reuse the existing handlers in
`src/http/handlers/link_handlers.cpp`.

## Tasks

| Task | Name | Category | Output |
|------|------|----------|--------|
| [01.0](/docs/roadmap/0004-web-ui/01.0-web-app-shell-static-hosting-shorten-flow/README.md) | Web App Shell, Static Hosting, and Shorten Flow | Foundation / MVP | A usable public `/app` page: paste URL → get short link → copy |
| [02.0](/docs/roadmap/0004-web-ui/02.0-fault-modes-validation-rate-limiting/README.md) | Fault Modes, Validation, and Public Create Rate Limiting | Robustness | Every failure path handled honestly in the UI; new config-gated public rate limit |
| [03.0](/docs/roadmap/0004-web-ui/03.0-link-preview/README.md) | Link Preview | Feature | A minimal `/app/preview/{slug}` page over the existing public preview endpoint |
| [04.0](/docs/roadmap/0004-web-ui/04.0-tests-ci-and-documentation/README.md) | Tests, CI, and Documentation | Quality | Consolidated test infra, a full happy-path-plus-fault e2e suite, and `docs/web-app/` |

## Shared contracts (authoritative)

Cross-cutting rules every task must honor. Unlike 0002, this milestone has
**no** fingerprint, RBAC, session, audit-log, or masking contracts — none of
those concepts exist in an unauthenticated public tool.

### SC1. Backend dependency: the existing public `/api/v1/links` API

The only backend link operations this milestone consumes are the three
already-public, no-auth endpoints, used exactly as they exist today:

- `POST /api/v1/links` — create. Returns the `serializeLinkViewJson` shape:
  `{ id, slug, url, short_url, status, redirect_type, ... }` (see
  `handleCreateLink` / `src/app/link_command_service.cpp`).
- `GET /api/v1/links/{slug}/preview` — preview. Returns
  `{ slug, url, status, redirect_type, enabled, expires_at, deleted_at }`
  (see `handlePreviewLink`).
- `GET /api/v1/links/{slug}` — fetch by slug (available if needed;
  `handleGetLinkBySlug`).

No new *read* endpoint is created by this milestone. The UI must treat these
response shapes as the contract and validate against them with Zod (task
01.0, subtask 03). If the backend response shape changes, the Zod schemas are
the single point of update.

### SC2. Error envelope (0004-specific)

Backend errors from the endpoints above use the envelope produced by
`makeApiErrorResponse` (`src/http/request_handlers.cpp`):

```json
{ "error": { "code": "...", "message": "...", "request_id": "..." } }
```

This is **distinct from 0002's admin C4 envelope**: there are no
auth-specific codes (`not_authenticated`, `csrf_failed`, …), and `request_id`
is *not* `req_`-prefixed — do not assume 0002's format. The frontend parses
this envelope with a dedicated Zod schema and surfaces `message` where safe
and `request_id` in a copyable "report this" affordance. The set of codes the
UI must map is enumerated in task
[02.0 subtask 02](/docs/roadmap/0004-web-ui/02.0-fault-modes-validation-rate-limiting/02-frontend-fault-state-model.md).

### SC3. Validation / SSRF opacity

The backend rejects both a malformed URL and a blocked (private-host) target
with the identical `invalid_url` 400. The UI **must not** attempt to tell
these apart or hint that a target was blocked for being private/internal — a
distinction would leak whether an internal host exists. Both conditions map to
one honest, non-leaky message (e.g. "That URL can't be shortened. Check that
it's a valid, publicly reachable http(s) address."). Client-side Zod
validation may pre-empt *obviously* malformed input (empty, non-http scheme)
for fast feedback, but must never claim to know why the server rejected a
syntactically plausible URL. See the fault-state model in task 02.0.

### SC4. Static hosting, path prefix, and router ordering

The SPA is served under the literal prefix `/app` and its nested asset paths
`/app/*`. `/admin` is reserved by 0002; `/{slug}` and `/r/{slug}` are the
protected redirect fast path; `/api/*` and observability routes are taken.

`src/http/router.cpp` dispatches by specificity (`pathSpecificity` counts
literal segments), so a literal `/app` already outranks the `/{slug}`
catch-all. **But** that router supports only a *single-segment* catch-all and
cannot serve nested SPA asset paths like `/app/assets/index-*.js`. Therefore
`/app` and `/app/*` require a **dedicated static-assets handler, mounted ahead
of the redirect and generic-fallback catch-alls** — the same pattern 0002
uses for `/admin`. The handler serves `index.html` for `/app` and unknown
`/app/*` sub-paths (SPA client-routing fallback) and serves hashed assets from
the built `web/app/dist/` directory. It must never shadow `/{slug}`,
`/api/*`, or observability routes. Specified in task
[01.0 subtask 02](/docs/roadmap/0004-web-ui/01.0-web-app-shell-static-hosting-shorten-flow/02-backend-static-hosting.md).

### SC5. Public-create rate limiting (new backend surface)

There is no rate limiting on `POST /api/v1/links` today. Because the create
form is public and unauthenticated, this milestone adds a **new**,
**config-gated**, **fail-open** rate-limiting check in the create path,
returning `429` in the SC2 error envelope with a `rate_limited` (or
equivalent) code. This is explicitly new backend work, not reuse of the
redirect-path limiter wiring. It must be disabled by default or safely
no-op when unconfigured, and must fail open (a limiter backend error never
blocks a legitimate create). Specified in task
[02.0 subtask 01](/docs/roadmap/0004-web-ui/02.0-fault-modes-validation-rate-limiting/01-backend-public-create-rate-limit.md).

### SC6. Non-goals (V1)

This UI is intentionally thin. Out of scope for V1:

- user accounts, sign-up, or login of any kind
- link history, "my links", or any notion of ownership
- editing, disabling, or deleting links from the UI
- analytics, click counts, charts, or dashboards
- any fingerprinting, tracking, or visitor identification
- QR codes, bulk import, or link customization beyond an optional slug
- SEO/marketing pages beyond the single shorten tool and the preview page

Anything requiring persistence of *who* created a link, or any analytics
surface, belongs to 0002, not here.

## Dependency graph

```text
01.0 Web App Shell + Static Hosting + Shorten Flow  *MVP*
        |
        +----------------------+
        v                      v
02.0 Fault Modes,        03.0 Link Preview
   Validation, and       (uses shell + API client
   Public Rate Limiting   from 01.0)
   (uses shell + API
    client from 01.0)
        |                      |
        +----------+-----------+
                   v
        04.0 Tests, CI, and Documentation
        (consolidates 01.0-03.0 component tests,
         adds full e2e + docs/web-app/)
```

`01.0` is the MVP and gates everything: it delivers the scaffold, the static
hosting, the API client + Zod schemas, and a working shorten flow. `02.0` and
`03.0` both build on 01.0's shell and API client and are independent of each
other (02.0 hardens the shorten flow; 03.0 adds the preview page). `04.0`
consolidates the per-task component tests into shared infra, adds the
Playwright e2e suite that must exercise the happy path *and every fault mode
from 02.0*, and writes the documentation.

The first useful deliverable — the MVP — is task `01.0` alone: a public page
that shortens a URL and lets the user copy the result.

## File map

```text
web/app/                                   # Public shorten-URL SPA (tasks 01.0-03.0)
  package.json
  tsconfig.json
  vite.config.ts
  index.html
  vitest.config.ts
  playwright.config.ts                     # config lands/consolidated in 04.0
  src/
    main.tsx
    app/            App.tsx, router.tsx, providers.tsx, config.ts
    api/            apiClient.ts, linksApi.ts, schemas.ts, errors.ts
    components/
      layout/       AppLayout.tsx, Header.tsx, Footer.tsx
      ui/           Button.tsx, Card.tsx, Input.tsx, Spinner.tsx,
                     CopyButton.tsx, EmptyState.tsx, ErrorState.tsx,
                     Toast.tsx / Toaster.tsx
      shorten/      ShortenForm.tsx, ShortenResultCard.tsx
      preview/      PreviewCard.tsx
    pages/          ShortenPage.tsx, PreviewPage.tsx, NotFoundPage.tsx
    model/          shortenForm.ts, linkView.ts, faultState.ts
    hooks/          useCreateLink.ts, usePreviewLink.ts, useCopyToClipboard.ts
    test/           msw/handlers.ts, msw/server.ts, fixtures/, setupTests.ts
  e2e/              shorten.spec.ts, fault-modes.spec.ts, preview.spec.ts

src/http/handlers/
  app_static_assets_handler.{h,cpp}        # New: serves /app and /app/* (task 01.0)
  link_handlers.cpp                        # Existing: reused, + optional 429 hook (task 02.0)

docs/web-app/                              # Task 04.0
  README.md, local-development.md, static-hosting.md, testing.md
```

## Global acceptance criteria

- [ ] A user can open `/app`, paste a long URL, submit, and see a short URL
      with a working copy-to-clipboard button — no login, no account.
- [ ] `/app` and nested `/app/*` asset paths are served by the new
      static-assets handler without shadowing `/{slug}`, `/api/*`, or
      observability routes.
- [ ] The shorten form supports an optional custom slug and validates input
      client-side with Zod before submitting.
- [ ] Every backend error code in the SC2 envelope, plus network / timeout /
      offline conditions, maps to a defined, honest UI state.
- [ ] A malformed URL and a blocked (private-host) target produce the *same*
      non-leaky message per SC3.
- [ ] A slug conflict (`409`), oversized body (`413`), over-length target,
      and rate-limited (`429`) response each surface an appropriate,
      distinct-where-safe UI state.
- [ ] The public-create rate limiter is new, config-gated, fail-open, and
      returns `429` in the standard error envelope; it is off/no-op by default.
- [ ] `/app/preview/{slug}` renders target URL / status / active state over
      the existing public preview endpoint, with correct not-found / expired /
      disabled states.
- [ ] `cd web/app && npm ci && npm run build` succeeds and produces
      `web/app/dist/`.
- [ ] Vitest component tests cover the happy path and every enumerated fault
      mode via MSW; the Playwright e2e suite exercises the happy path and each
      fault mode, not just a smoke test.
- [ ] CI runs frontend lint/typecheck/unit/build and the e2e suite.
- [ ] `docs/web-app/` documents local dev, the `/app` static-hosting contract,
      and how this UI relates to 0002's `/admin` and the `/api/v1/links` API.
