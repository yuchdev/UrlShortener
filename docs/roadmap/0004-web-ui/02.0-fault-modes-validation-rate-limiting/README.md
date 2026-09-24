# Task 02.0 - Fault Modes, Validation, and Public Create Rate Limiting

**Parent milestone:** [Milestone 0004 - Public Web UI](/docs/roadmap/0004-web-ui/plan.md)
**Status:** ⬜ Not started

## Scope

Make the shorten flow honest under failure. This task adds the one new backend
surface a public create form needs — a config-gated, fail-open rate limiter
returning `429` — and builds the frontend fault-state model that maps every
backend error code plus network/timeout/offline/clipboard conditions to a
defined UI state, then wires that model into the shorten UI and covers each
state with component tests.

Fault modes are a first-class deliverable of this milestone, not an
afterthought. Two facts drive the design and must be honored exactly:

- **SSRF opacity** — malformed URL and blocked private-host target both return
  the same `invalid_url` 400; the UI shows one honest, non-leaky message for
  both (see [plan.md SC3](/docs/roadmap/0004-web-ui/plan.md)).
- **No existing create rate limit** — `429` requires new backend work, stated
  plainly as new, not as reuse of the redirect-path limiter (see
  [plan.md SC5](/docs/roadmap/0004-web-ui/plan.md)).

## Subtasks

| # | Document | Status | Depends on | Blocks |
|---|----------|--------|------------|--------|
| 01 | [Backend public-create rate limit](01-backend-public-create-rate-limit.md) | ⬜ Not started | 01.0 | 02 |
| 02 | [Frontend fault-state model](02-frontend-fault-state-model.md) | ⬜ Not started | 01.0/03 | 03 |
| 03 | [Wire fault states into shorten UI](03-wire-fault-states-into-shorten-ui.md) | ⬜ Not started | 02 | 04 |
| 04 | [Fault-mode component tests](04-fault-mode-component-tests.md) | ⬜ Not started | 03 | none |

## Key constraints

- The rate limiter is **new** backend work: config-gated, off/no-op by
  default, and fail-open — a limiter error never blocks a legitimate create.
  It returns `429` in the standard SC2 envelope (see
  [plan.md SC5](/docs/roadmap/0004-web-ui/plan.md)). It is *not* the
  redirect-path limiter reused.
- Malformed and blocked-target URLs map to **one** shared message; the UI must
  never hint that a target was blocked for being private/internal (see
  [plan.md SC3](/docs/roadmap/0004-web-ui/plan.md)).
- The fault-state model is a typed, exhaustive mapping — every SC2 code plus
  transport conditions has a defined state; no unhandled/`else` that silently
  swallows a failure.
- No secrets, DSNs, IPs, or `request_id` internals leaked into UI copy beyond
  the safe `message` and the opaque `request_id` string itself.
