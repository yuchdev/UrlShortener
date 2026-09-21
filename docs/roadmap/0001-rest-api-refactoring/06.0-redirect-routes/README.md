# Task 06.0 - Redirect routes

**Parent milestone:** [Milestone 0001 - REST API Refactoring](/docs/roadmap/0001-rest-api-refactoring/plan.md)
**Status:** ✅ Complete

## Scope

Extract `GET /r/{slug}` (prefixed redirect) and `GET /{slug}` (root redirect,
with fallthrough to `handleApplicationRequest()`) into dedicated handlers, and
register them behind an explicit guard before switching production dispatch.
This is the highest-risk task in the milestone: the redirect fast path must
stay narrow and free of management-plane logic, and its performance and
security posture must not regress. A `security-auditor` pass was required and
passed before this task was considered complete.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Extract prefixed redirect handler](01-extract-prefixed-redirect-handler.md) | ✅ Complete | 03 |
| 02 | [Extract root redirect handler with fallback](02-extract-root-redirect-handler.md) | ✅ Complete | 03 |
| 03 | [Register redirect routes behind explicit guard](03-register-redirect-routes-guarded.md) | ✅ Complete | none |

## Key constraints

- Handler files do not include link-management handler headers.
- No JSON link serialization is added to the redirect success path.
- Root redirect fallthrough (non-GET, invalid slug, `/api/...`) remains
  explicit in the handler, not implicit in dispatch order.
- Redirect route labels remain `redirect_prefixed` and `redirect_root`.
- `feature-reviewer` and `security-auditor` passes are required before
  switching production dispatch (subtask 03).

## Review evidence

Security review:
[docs/security/2026-07-17-redirect-route-refactor.md](/docs/security/2026-07-17-redirect-route-refactor.md)
- Verdict: PASS.
