# Task 04.0 - Link management routes

**Parent milestone:** [Milestone 0001 - REST API Refactoring](/docs/roadmap/0001-rest-api-refactoring/plan.md)
**Status:** ✅ Complete

## Scope

Extract and route the full canonical `/api/v1/links` surface: read by id/slug,
preview, create, patch, delete, lifecycle actions (enable/disable/restore),
stats, and the QR/routing placeholders. This is the largest handler-extraction
task in the milestone, covering both read-only and mutating endpoints, and it
is where the bulk of existing validation and serialization logic moves out of
`handleShortenerRequest()` into named, independently testable handlers.
Compatibility endpoints (`/api/v1/short-urls`) are explicitly out of scope
here and stay unmigrated until Task 05.0.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Extract canonical read and preview handlers](01-extract-read-preview-handlers.md) | ✅ Complete | 04 |
| 02 | [Extract create, patch, and delete handlers](02-extract-create-patch-delete-handlers.md) | ✅ Complete | 04 |
| 03 | [Extract lifecycle, stats, and placeholder handlers](03-extract-actions-stats-placeholders.md) | ✅ Complete | 04 |
| 04 | [Register and switch canonical link routes](04-register-and-switch-canonical-routes.md) | ✅ Complete | none |

## Key constraints

- No mutation occurs in the read/preview handlers (subtask 01).
- No JSON dependency is introduced (subtask 02).
- The compatibility `code` field is not handled here; that is Task 05.0.
- `handleLinkStats` receives its query string from `RouteContext::query_string`,
  not from re-parsing the raw target (subtask 03).
- No compatibility endpoint is changed by this task (subtask 04).

## Implementation note

The three handler-extraction subtasks (01-03) each proposed their own test
file (`07_link_read_preview_handlers.cpp`, `08_link_mutation_handlers.cpp`,
`09_link_action_handlers.cpp`). The shipped implementation consolidates all
three case lists into a single `tests/unit/http/10_link_handlers.cpp`. No
coverage was dropped; see
[status.md](/docs/roadmap/0001-rest-api-refactoring/status.md) for the full
reconciliation note.
