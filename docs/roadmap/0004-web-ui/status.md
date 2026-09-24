# Milestone 0004 - Public Web UI - Status

Tracks progress against [plan.md](/docs/roadmap/0004-web-ui/plan.md).

## Current status

| Task | Name | Status | Tests |
|------|------|--------|-------|
| 01.0 | Web App Shell, Static Hosting, and Shorten Flow | ⬜ Not started | 0 |
| 02.0 | Fault Modes, Validation, and Public Create Rate Limiting | ⬜ Not started | 0 |
| 03.0 | Link Preview | ⬜ Not started | 0 |
| 04.0 | Tests, CI, and Documentation | ⬜ Not started | 0 |

**Legend:** ✅ Complete · 🔶 In progress / partial · ⬜ Not started

**Current gate status:** 0% implemented. This milestone is design-complete
(4 tasks decomposed into 14 subtasks) but has no code yet. Verified against
the codebase as of 2026-09-24: there is no `web/app/` and no `package.json`
in the repository; the three `/api/v1/links` endpoints this milestone reuses
(`handleCreateLink`, `handlePreviewLink`, `handleGetLinkBySlug` in
`src/http/handlers/link_handlers.cpp`) exist and require no auth today. Scope
is the MVP first (task 01.0: shell + static hosting + shorten flow), then
fault-mode hardening plus the new rate limiter (02.0), the preview page
(03.0), and test/CI/docs consolidation (04.0). Stack decisions match 0002
exactly: React + TypeScript + Vite + TanStack Router/Query + React Hook Form
+ Zod + Tailwind CSS with shadcn/ui-style internal wrappers.

## Notes & decisions

- **SSRF rejection is opaque, and the UI must keep it that way.**
  `normalizeTargetUrl` (`src/core/utils.cpp`) rejects private-host targets
  (`isPrivateHost`, unless `SHORTENER_ALLOW_PRIVATE_TARGETS`) with the *same*
  `invalid_url` 400 as a plainly malformed URL. The client cannot distinguish
  "malformed" from "blocked internal target" and must not try — doing so would
  leak whether an internal host exists. Both map to one honest, non-leaky
  message. This is load-bearing for the fault-state model, not a cosmetic
  copy choice. See [plan.md SC3](/docs/roadmap/0004-web-ui/plan.md).
- **There is no rate limiting on link creation today.** `IRateLimiter` /
  `RateLimitDecision` is wired only into the redirect path
  (`src/http/handlers/redirect_handlers.cpp`), *not* into
  `POST /api/v1/links`. A public unauthenticated create form is an abuse
  vector, so the mandatory `429` fault mode requires a genuinely new,
  config-gated, fail-open backend surface (task 02.0 subtask 01) — stated as
  new work, not as reuse of the redirect limiter. See
  [plan.md SC5](/docs/roadmap/0004-web-ui/plan.md).
- **Reuse, not fork, of `/api/v1/links`.** The three endpoints are already
  public and no-auth; the UI consumes them directly and pins their response
  shapes with Zod. No new *read* endpoint is created. See
  [plan.md SC1](/docs/roadmap/0004-web-ui/plan.md) and
  [00-decision-record.md §3](/docs/roadmap/0004-web-ui/00-decision-record.md).
- **Relationship to 0002.** This milestone borrows 0002's *documented*
  static-SPA-under-a-path-prefix pattern for serving `/app`, but 0002 is
  itself unimplemented (no `web/admin/`, no `src/admin/`). 0004 references
  0002's pattern, never its code. `/app` (public shorten tool) and `/admin`
  (login-gated analytics console) are separate surfaces with no shared data,
  auth, or analytics.
- **V1 non-goals.** No accounts, no link history/ownership, no editing/
  disabling/deleting from the UI, no analytics/charts, no fingerprinting.
  See [plan.md SC6](/docs/roadmap/0004-web-ui/plan.md).

## Decomposition tree (as built)

```text
docs/roadmap/0004-web-ui/
  plan.md
  status.md
  00-decision-record.md
  01.0-web-app-shell-static-hosting-shorten-flow/   (5 subtasks)
    README.md
    01-frontend-app-shell.md
    02-backend-static-hosting.md
    03-api-client-and-schemas.md
    04-shorten-flow-ui.md
    05-happy-path-component-tests.md
  02.0-fault-modes-validation-rate-limiting/        (4 subtasks)
    README.md
    01-backend-public-create-rate-limit.md
    02-frontend-fault-state-model.md
    03-wire-fault-states-into-shorten-ui.md
    04-fault-mode-component-tests.md
  03.0-link-preview/                                (2 subtasks)
    README.md
    01-frontend-preview-page.md
    02-tests.md
  04.0-tests-ci-and-documentation/                  (3 subtasks)
    README.md
    01-frontend-test-infrastructure.md
    02-playwright-e2e-suite.md
    03-documentation.md
```

4 task folders, 14 subtask files total.

## Per-task detail

Nothing is implemented yet. Each task folder's `README.md` carries its own
`⬜ Not started` status and subtask table; see the individual subtask files
for concrete file lists, requirements, and per-subtask success criteria.
This section will gain per-task narrative detail once a task moves past
`⬜ Not started`.
