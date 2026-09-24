# 02 - Playwright E2E Suite

**Parent task:** 04.0 Tests, CI, and Documentation
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** none

## Objective

Add a Playwright end-to-end suite that drives the real built app under `/app`
and covers the happy path **and every fault mode** from task 02.0's
fault-state model — not just a smoke test — plus the preview page.

## Files to add

```text
web/app/e2e/shorten.spec.ts
web/app/e2e/fault-modes.spec.ts
web/app/e2e/preview.spec.ts
```

## Requirements

1. `shorten.spec.ts` — happy path: open `/app`, paste a valid URL (and a
   run with a custom slug), submit, assert the result card and the short URL,
   and exercise the copy button.
2. `fault-modes.spec.ts` — one scenario per `FaultState` from
   [02.0 subtask 02](/docs/roadmap/0004-web-ui/02.0-fault-modes-validation-rate-limiting/02-frontend-fault-state-model.md),
   driven by intercepting/mocking the backend response (Playwright route
   interception mirroring the shared fault catalogue): `invalid_url` (asserting
   the **same** message for the malformed and the blocked-target case —
   SSRF-opacity, a required scenario), `slug_conflict` (`409`),
   `body_too_large` (`413`), `target_too_long`, `rate_limited` (`429` with
   retry guidance), `server_error` (`5xx` with copyable `request_id`),
   `network_error`, `timeout`, and the `clipboard_unavailable` fallback.
3. `preview.spec.ts` — `/app/preview/{slug}` for an active link plus the
   not-found and an inactive (disabled/expired) state.
4. The suite runs against the built app served under `/app` (via the config
   from subtask 01), validating the static-hosting path, not just the dev
   server.
5. Assert user-visible outcomes (visible message text, which surface), not
   internal state.

## Constraints

- No dependence on a live external network; back the backend responses with
  Playwright route interception / a stub, mirroring the component-test fault
  catalogue so the two stay in sync.
- A smoke test alone does not satisfy this subtask — every enumerated fault
  mode must have a scenario.

## Success criteria

- [ ] Happy path (with and without custom slug) passes e2e against the
      `/app`-served build.
- [ ] Every `FaultState` has a corresponding e2e scenario, including the
      SSRF-opacity identical-message case.
- [ ] The preview page's active, not-found, and inactive states are covered.
- [ ] `npm run test:e2e` is green locally and in CI.
