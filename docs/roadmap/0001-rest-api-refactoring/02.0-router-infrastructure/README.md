# Task 02.0 - Router infrastructure

**Parent milestone:** [Milestone 0001 - REST API Refactoring](/docs/roadmap/0001-rest-api-refactoring/plan.md)
**Status:** ✅ Complete

## Scope

Add the router mechanics - `RouteContext`, handler type aliases, path-pattern
matching, dispatch, `RouterBuilder`, and registry/router consistency checks -
without migrating any real production dispatch. `handleShortenerRequest()`
keeps using the old branch chain throughout this task; only stub handlers are
exercised by the new `Router`. This gives later migration tasks (03.0-07.0) a
tested, dependency-free router to register real handlers against.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Add route context and handler type aliases](01-route-context-and-handler-types.md) | ✅ Complete | 02, 03, 04 |
| 02 | [Implement path-pattern matching](02-router-matcher.md) | ✅ Complete | 03, 04 |
| 03 | [Add Router dispatch and RouterBuilder](03-router-dispatch-and-builder.md) | ✅ Complete | 04 |
| 04 | [Add registry and router consistency checks](04-registry-consistency-tests.md) | ✅ Complete | none |

## Key constraints

- No changes to `handleShortenerRequest()` in this task.
- No new external dependencies.
- `Router` has no link repository, cache, analytics, or TLS dependency.
- `RouterBuilder` is the only file that knows registration order.
- New public symbols carry Doxygen comments.
