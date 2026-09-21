# Task 05.0 - Compatibility routes

**Parent milestone:** [Milestone 0001 - REST API Refactoring](/docs/roadmap/0001-rest-api-refactoring/plan.md)
**Status:** ✅ Complete

## Scope

Migrate `/api/v1/short-urls` compatibility create/read behavior into named
handlers and route them through `Router`, while preserving support for the
legacy `code` field and keeping response shapes equivalent to the canonical
`/api/v1/links` endpoints. This task depends on Task 04.0's canonical create
handler, since compatibility create reuses shared create logic rather than
duplicating it.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Extract compatibility create handler](01-extract-compat-create-handler.md) | ✅ Complete | 03 |
| 02 | [Extract compatibility read handler](02-extract-compat-read-handler.md) | ✅ Complete | 03 |
| 03 | [Register and switch compatibility routes](03-register-and-switch-compat-routes.md) | ✅ Complete | none |

## Key constraints

- Existing `/api/v1/links` create tests still pass unchanged.
- No response field is renamed from the compatibility endpoint.
- The canonical read handler is not duplicated unless necessary.
- Redirect and fallback branches remain unchanged by this task.

## Implementation note

The proposed `10_compat_create_handler.cpp` and `11_compat_read_handler.cpp`
test files were consolidated into a single
`tests/unit/http/11_compatibility_handlers.cpp` in the shipped implementation
(test numbering also shifted because Task 02.0's consistency-test file became
`08_router_registry_consistency.cpp` rather than `05_router_registry_consistency.cpp`).
No coverage was dropped.
