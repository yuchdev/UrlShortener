# 02 - Frontend Fault-State Model

**Parent task:** 02.0 Fault Modes, Validation, and Public Create Rate Limiting
**State:** ⬜ Not started
**Depends on:** 01.0 subtask 03 (typed error), 02.0 subtask 01 (429 exists)
**Blocks:** 03

## Objective

Define a typed, exhaustive mapping from every possible backend error code plus
every transport/client condition to a well-defined UI fault state. This is a
pure model/logic subtask — no rendering (that is subtask 03).

## Files to add

```text
web/app/src/model/faultState.ts
web/app/src/api/faultMapping.ts
```

## Requirements

1. `faultState.ts` defines a discriminated-union `FaultState` type covering at
   minimum:
   - `invalid_url` — malformed URL **or** blocked private-host target. Per
     [plan.md SC3](/docs/roadmap/0004-web-ui/plan.md) these are
     **indistinguishable** and share one honest, non-leaky message (e.g.
     "That URL can't be shortened. Check that it's a valid, publicly reachable
     http(s) address."). The model must **not** carry a "blocked because
     private" variant.
   - `slug_conflict` — slug already taken (`409`); actionable ("try a
     different custom slug").
   - `body_too_large` — oversized request body (`413`); guidance to shorten
     the URL.
   - `target_too_long` — URL exceeds the max target length; same family as
     above, distinct message where the backend distinguishes it.
   - `rate_limited` — `429` from the new limiter; message with retry guidance
     (honor `Retry-After` if present).
   - `server_error` — `5x` / `internal_error`; generic "something went wrong,
     try again", surfacing the opaque `request_id` for support.
   - `network_error` — fetch failed / offline / DNS.
   - `timeout` — request exceeded the client timeout.
   - `clipboard_unavailable` — copy failed / Clipboard API absent (drives the
     select-text fallback).
   - `unknown` — a defined fallback for any code not explicitly mapped, so no
     failure is ever silently swallowed.
2. `faultMapping.ts` maps the typed error from
   [01.0 subtask 03](/docs/roadmap/0004-web-ui/01.0-web-app-shell-static-hosting-shorten-flow/03-api-client-and-schemas.md)
   (`code` + HTTP status) and transport failures into a `FaultState`. The
   mapping must be total: every SC2 code and every transport condition
   resolves to exactly one state, defaulting to `unknown`.
3. Each `FaultState` carries: a stable discriminant, a user-facing message, a
   presentation hint (`field` | `toast` | `page`) consumed by subtask 03, and
   optional `request_id` / `retryAfter` payload where relevant.
4. Copy must be honest and non-leaky: never reveal that a target was blocked
   for being internal/private; never expose DSNs, IPs, stack traces, or the
   analytics salt.

## Constraints

- Model/logic only — no React components, no rendering.
- Exhaustiveness enforced at the type level (e.g. a `never`-exhaustive switch)
  so a new code cannot compile without a mapping decision.
- Do not conflate the two `invalid_url` causes; that is a correctness/security
  requirement, not a copy preference.

## Success criteria

- [ ] `FaultState` is a discriminated union covering all states above.
- [ ] `faultMapping.ts` maps every SC2 code + transport condition to exactly
      one `FaultState`, with `unknown` as the total fallback.
- [ ] Malformed and blocked-target inputs both resolve to the single
      `invalid_url` state with one shared message.
- [ ] Each state carries a presentation hint (`field` / `toast` / `page`) for
      subtask 03.
- [ ] Unit tests (colocated) assert the mapping for each code/condition,
      including the SSRF-opacity case.
