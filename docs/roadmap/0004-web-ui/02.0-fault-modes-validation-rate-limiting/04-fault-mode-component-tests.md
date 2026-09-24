# 04 - Fault-Mode Component Tests

**Parent task:** 02.0 Fault Modes, Validation, and Public Create Rate Limiting
**State:** ⬜ Not started
**Depends on:** 03
**Blocks:** none

## Objective

Cover every fault state from subtask 02 with Vitest component tests, each
exercised via an MSW handler (or simulated transport condition) that produces
the corresponding backend/transport failure.

## Files to add

```text
web/app/src/components/shorten/ShortenForm.faults.test.tsx
web/app/src/components/shorten/ShortenResultCard.clipboard.test.tsx
web/app/src/test/msw/faultHandlers.ts
```

## Requirements

1. Extend the MSW setup (from
   [01.0 subtask 05](/docs/roadmap/0004-web-ui/01.0-web-app-shell-static-hosting-shorten-flow/05-happy-path-component-tests.md))
   with per-fault handlers returning the SC2 envelope for each code: `400`
   `invalid_url` (used for both malformed and blocked-target cases), `409`
   `slug_conflict`, `413` `body_too_large`, over-length target, `429`
   `rate_limited` (with `Retry-After`), and `500` `internal_error`.
2. Assert the rendered surface for each state matches subtask 03's routing:
   - both malformed and blocked-target `invalid_url` responses render the
     **same** inline URL-field message (the SSRF-opacity test — a named,
     required case);
   - `409` → slug-field error; `413`/over-length → shorten-URL guidance;
     `429` → retry toast honoring `Retry-After`; `500`/`unknown` → full error
     surface with copyable `request_id`.
3. Simulate transport conditions: a network failure (MSW error/abort), a
   timeout, and offline — asserting `network_error` / `timeout` toasts.
4. Clipboard test: mock an unavailable/throwing Clipboard API and assert the
   `clipboard_unavailable` fallback (selectable text + toast).
5. Every state in the `FaultState` union must have at least one test; a
   missing state is a coverage gap this subtask must close.

## Constraints

- MSW/mocks only; no real backend, no real network.
- Tests assert user-visible behavior (message text, which surface), not
  internal mapping details already unit-tested in subtask 02.
- Keep handlers reusable so task 04.0's e2e suite can mirror the same fault
  catalogue.

## Success criteria

- [ ] Each `FaultState` is exercised by at least one component test via MSW or
      a simulated transport condition.
- [ ] The SSRF-opacity case (malformed vs blocked-target → identical message)
      is explicitly asserted.
- [ ] `429` retry guidance, clipboard fallback, and full-page `5xx` surface
      are all asserted.
- [ ] `cd web/app && npm run test` is green.
