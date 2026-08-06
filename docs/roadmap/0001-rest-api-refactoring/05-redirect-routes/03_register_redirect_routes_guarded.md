# Task 05.03 - Register redirect routes behind explicit guard

## Objective

Register redirect routes, but switch production dispatch only after tests and
review confirm fast-path safety.

## Files to modify

- `src/http/RouterBuilder.cpp`
- `src/http/request_handlers.cpp`
- `tests/unit/http/04_redirect_fallback_characterization.cpp`
- `tests/unit/http/05_router_registry_consistency.cpp`

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

## Acceptance criteria

- Redirect route labels remain `redirect_prefixed` and `redirect_root`.
- No measurable behavior difference in redirect tests.

