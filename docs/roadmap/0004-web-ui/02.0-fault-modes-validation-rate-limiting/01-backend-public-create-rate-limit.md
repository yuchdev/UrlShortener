# 01 - Backend Public-Create Rate Limit

**Parent task:** 02.0 Fault Modes, Validation, and Public Create Rate Limiting
**State:** ⬜ Not started
**Depends on:** 01.0
**Blocks:** 02
**Category:** New backend work (handed to `cpp-expert`)

## Objective

Add a config-gated, fail-open rate-limiting check to the public
`POST /api/v1/links` create path that returns `429` in the standard error
envelope when a client exceeds the configured budget. This is **new** backend
work — link creation has no rate limiting today, and `IRateLimiter` is
currently wired only into the redirect path
(`src/http/handlers/redirect_handlers.cpp`). This subtask does *not* reuse
that wiring; it adds a distinct check in the create handler.

## Files to add / edit

```text
src/http/handlers/link_handlers.cpp   # add the rate-limit check in handleCreateLink
src/core/config.h                     # add create-rate-limit config fields (edit)
src/cli_parser.cpp                    # parse the new flags (edit)
sources.cmake                         # only if a new .cpp is introduced
```

`cpp-expert` decides the exact placement and whether a small helper file is
warranted; the contract below is what matters.

## Requirements

1. Introduce a config-gated public-create rate limit. It must be:
   - **Off / no-op by default** — existing deployments see no behavior change
     until it is explicitly enabled.
   - **Config-gated** via `ServerConfig`/CLI flags (e.g. an enable flag plus
     limit + window), consistent with existing request-hardening flags.
   - **Fail-open** — if the limiter backend errors or is unavailable, the
     create proceeds. A limiter failure must never block a legitimate create.
2. When enabled and the budget is exceeded, `handleCreateLink` returns HTTP
   `429` using `makeApiErrorResponse` (the SC2 envelope,
   `src/http/request_handlers.cpp`) with a `rate_limited` (or equivalently
   named) `code` and a safe `message`. Include standard rate-limit response
   semantics (e.g. a `Retry-After` header) if it fits the existing handler
   conventions.
3. The keying strategy (per-client-IP or equivalent) must reuse the existing
   `IRateLimiter::Allow(key, limit, window)` abstraction and
   `RateLimitDecision` model — the *interface* is reused, but this is a new
   *call site* in the create path.
4. Do not weaken or alter the redirect fast path or its existing limiter.
5. Do not log client IPs or the rate-limit key in a way that stores/leaks PII
   beyond existing conventions; follow the analytics/observability redaction
   rules already in the codebase.

## Constraints

- Coding guardrails: no cross-layer leakage (limiter access stays behind
  `IRateLimiter`), parameterized/typed models over ad-hoc strings, minimal
  surface — do not build a speculative rate-limiting framework.
- Must be safe to ship disabled; enabling it is a deployment decision.
- `app-architect` specifies; `cpp-expert` implements and `testing-expert`
  covers with backend unit/integration tests (that backend test work is
  tracked under task 04.0's CI/test consolidation, but the handler-level
  behavior is asserted here).

## Success criteria

- [ ] With the feature disabled (default), `POST /api/v1/links` behaves
      exactly as today.
- [ ] With the feature enabled, exceeding the budget returns `429` in the SC2
      envelope with a `rate_limited` code and safe message.
- [ ] A simulated limiter backend failure results in the create proceeding
      (fail-open), not a `429` or `500`.
- [ ] The redirect fast path and its existing limiter are unchanged.
- [ ] New config flags are parsed and documented in the handler/config help.
