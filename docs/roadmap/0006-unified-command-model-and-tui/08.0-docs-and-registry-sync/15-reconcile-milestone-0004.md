# 15 - Reconcile Milestone 0004

**Parent task:** 08.0 Docs and registry sync
**State:** ⬜ Not started
**Depends on:** plan.md "Impact on other milestones"
**Blocks:** 16
**Estimated size:** S
**Suggested agent:** docs-updater (edits), app-architect (decides the rate-limit placement)

## Objective

Record the (small) effect of the command model on the public web UI
milestone: no blocker, `POST /api/v1/links` unchanged, optional use of the
generic endpoint, and a precise statement of where SC5 public-create rate
limiting sits now that REST create goes through the registry.

## Dependencies

- `docs/roadmap/0004-web-ui/plan.md` already carries a "Note (added with Milestone 0006)" after the decision-record paragraph; SC5 text (config-gated, fail-open limiter in the create path, `429` in the SC2 envelope, `rate_limited` code); `02.0-fault-modes-validation-rate-limiting/README.md` + `01-backend-public-create-rate-limit.md`; `status.md`.
- 0006 plan C3 (existing REST bodies/statuses unchanged), C1 (no per-command logic in transports), C7 (authorization in the registry).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `docs/roadmap/0004-web-ui/plan.md` | modify | note, SC5 paragraph |
| `docs/roadmap/0004-web-ui/02.0-fault-modes-validation-rate-limiting/01-backend-public-create-rate-limit.md` | modify | hook location |
| `docs/roadmap/0004-web-ui/status.md` | modify | dependency note |

## Classes and functions (exact edits)

1. **plan.md note:** keep, and append "`POST /api/v1/links`, `GET /api/v1/links/{slug}/preview` keep exact request/response bodies and status codes (Milestone 0006 contract C3), so this milestone's API client needs no change. The shorten UI may keep calling the resource routes; the generic endpoint is optional."
2. **SC5:** append: "After Milestone 0006 the REST create route is served by `CommandRegistry::execute` (`link.create`). The limiter is **transport policy, not command logic**: it runs in the REST adapter before `execute` for the routes mapped to `link.create`, keyed by client address, using the existing `IRateLimiter` port (fail-open). This keeps CLI/TUI/stdio (local operator) unlimited and keeps one rule for both REST entry points (`POST /api/v1/links` and `POST /api/v1/commands` with `link.create`)." (0006 plan.md "Impact" and the 0004 plan note already use this adapter-level wording on the design branch; open decision D5 asks the user to confirm it.)
3. **02.0 subtask 01 (rate limit):** wherever it names the create-path hook (`handleCreateLink` in `src/http/handlers/link_handlers.cpp`), add "after Milestone 0006 the hook is a REST-adapter pre-execute policy keyed on the route->command table entry for `link.create`; it must also cover `POST /api/v1/commands` bodies whose `command` is `link.create` (inspect the envelope's `command` only; do not parse args)". Add a test requirement: both entry points hit the same limiter bucket.
4. **status.md:** add under Current gate status: "Soft dependency: Milestone 0006 (runs first; no code blocker)."

## Implementation steps

1. Re-read the three files; 2. apply edits; 3. `/link-check docs/roadmap/0004-web-ui`.

## Commands

```bash
git grep -n "handleCreateLink\|rate_limited" -- docs/roadmap/0004-web-ui
```

## Tests

None executable.

## Constraints

- No change to 0004's UI scope or error model (SC2/SC3).
- Absolute-from-root links.

## Success criteria

1. SC5 states where the limiter lives and that both REST entry points share it.
2. No 0004 document refers to a "command-level" limiter inside the handler.

## Out of scope / risks

- **Design problem:** `CallerContext{principal, ControlSet, transport}` carries no client address, so a limiter inside the registry cannot key by IP without extending `CallerContext`. The adapter-level placement above avoids that; it is recorded as open decision D5 for user confirmation.
- `POST /api/v1/commands` could be used to bypass a route-keyed limiter unless the adapter inspects `command`; covered by the edit and test requirement above.
