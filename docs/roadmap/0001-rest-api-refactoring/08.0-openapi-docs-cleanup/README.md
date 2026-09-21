# Task 08.0 - OpenAPI/docs cleanup

**Parent milestone:** [Milestone 0001 - REST API Refactoring](/docs/roadmap/0001-rest-api-refactoring/plan.md)
**Status:** ✅ Complete

## Scope

Close out the milestone: extend `RouteDescriptor` with documentation
metadata, generate an API reference from that metadata, remove the now-dead
branch-chain code from `handleShortenerRequest()`, and bring public docs and
review gates (`feature-reviewer`, `security-auditor`, `docs-updater`,
`test-gap`) in sync with the final routing architecture. This is the task
that turns `handleShortenerRequest()` into the one-line
`applicationRouter().dispatch(...)` delegation and confirms no route retains
two active implementations.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Extend RouteDescriptor for documentation metadata](01-extend-route-metadata.md) | ✅ Complete | 02 |
| 02 | [Generate API reference from RouteRegistry](02-generate-api-reference-from-registry.md) | ✅ Complete | 03 |
| 03 | [Remove obsolete branch-chain dispatch](03-remove-obsolete-branch-chain.md) | ✅ Complete | 04 |
| 04 | [Update public docs and complete review gates](04-update-public-docs-and-review.md) | ✅ Complete | none |

## Key constraints

- No new JSON/OpenAPI dependency; existing route label tests still pass
  (subtask 01).
- No JSON library added for the documentation generator; no runtime
  `GET /api/v1/openapi.json` unless a review decision explicitly approves it
  (subtask 02).
- No route retains a duplicate old and new implementation after cleanup
  (subtask 03).
- `request_handlers.cpp` remains buildable and simpler after cleanup.
- Docs match the final routing architecture; review findings are resolved or
  explicitly deferred (subtask 04).

## Review evidence

Security review for the overall route refactor:
[docs/security/2026-07-17-rest-api-route-refactor.md](/docs/security/2026-07-17-rest-api-route-refactor.md)
- Verdict: PASS.
