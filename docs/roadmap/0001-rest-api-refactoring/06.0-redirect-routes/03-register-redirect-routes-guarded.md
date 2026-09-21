# 03 - Register redirect routes behind explicit guard

**Parent task:** 06.0 Redirect routes
**State:** ✅ Complete
**Depends on:** 01, 02
**Blocks:** none

## Objective

Register redirect routes, but switch production dispatch only after tests and
review confirm fast-path safety.

## Files to modify

- `src/http/RouterBuilder.cpp`
- `src/http/request_handlers.cpp`
- `tests/unit/http/04_redirect_fallback_characterization.cpp`
- `tests/unit/http/05_router_registry_consistency.cpp`

Implementation note: shipped as `src/http/router_builder.cpp` (renamed to
snake_case by commit `3f4c170 Rename files to snake_style`) and
`tests/unit/http/08_router_registry_consistency.cpp` (renumbered to follow
the router-infrastructure test files, see Task 02.0). The characterization
test file matches its originally proposed name.

## Routes to register

```text
GET /r/{slug}
GET /{slug}
```

## Dispatch approach

Preferred sequence:

1. Register routes in `RouterBuilder` and pass consistency tests.
2. Keep old redirect branches active in production for one commit.
3. In a follow-up commit, switch only `/r/{slug}` to router dispatch.
4. Switch root `/{slug}` only after fallback tests pass.

## Tests

- All redirect characterization tests.
- Registry/router consistency tests.
- Endpoint matrix tests.

## Review requirements

- Run `feature-reviewer`.
- Run `security-auditor` because redirect handlers touch untrusted slugs and
  external redirect targets.

## Constraints

- Redirect route labels remain `redirect_prefixed` and `redirect_root`.
- No measurable behavior difference in redirect tests.

## Success criteria

- [x] `src/http/router_builder.cpp` registers `GET /r/{slug}` (label
      `redirect_prefixed`) before `GET /{slug}` (label `redirect_root`).
- [x] `handleShortenerRequest()` dispatches both redirect routes through the
      router.
- [x] `tests/unit/http/04_redirect_fallback_characterization.cpp` and
      `tests/unit/http/08_router_registry_consistency.cpp` pass.
- [x] Redirect route labels remain `redirect_prefixed` and `redirect_root`.
- [x] Security review completed with no blocking findings:
      [docs/security/2026-07-17-redirect-route-refactor.md](/docs/security/2026-07-17-redirect-route-refactor.md)
      - Verdict: PASS.
- [x] No measurable behavior difference in redirect tests.
