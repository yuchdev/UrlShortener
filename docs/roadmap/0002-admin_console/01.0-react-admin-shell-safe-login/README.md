# Task 01.0 - React Admin Shell and Safe Login Integration

**Parent milestone:** [Milestone 0002 - Admin Console](/docs/roadmap/0002-admin_console/plan.md)
**Status:** ⬜ Not started

## Scope

Create the React admin application shell and connect it to the existing safe
login system, so that `/admin` becomes usable end to end (login, protected
navigation, logout) before any analytics exist. This task does not implement
analytics data - `DashboardPage` and `SettingsPage` are shells populated by
later tasks (03.0, 08.0).

The shell establishes every cross-cutting piece every later page depends on:
the React app scaffold (Vite/TanStack Router/TanStack Query providers), the
backend session/permission middleware, the `AuthProvider` / `ProtectedRoute` /
`PermissionGate` trio, and the layout chrome (`Sidebar`, `TopBar`). See
[plan.md - Shared contract C2](/docs/roadmap/0002-admin_console/plan.md) for
the auth/session model this task must implement exactly.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Frontend app shell](01-frontend-app-shell.md) | ⬜ Not started | 03 |
| 02 | [Backend session auth](02-backend-session-auth.md) | ⬜ Not started | 03 |
| 03 | [Frontend auth integration](03-frontend-auth-integration.md) | ⬜ Not started | 04 |
| 04 | [Tests and e2e](04-tests-and-e2e.md) | ⬜ Not started | none |

## Key constraints

- No browser-side credential storage; auth state is derived only from the
  server session via `GET /auth/me` - never from `localStorage`.
- Reuse the safe-login verifier from the prior safe-login task instead of
  reimplementing credential checking; if equivalent files already exist,
  extend them instead of duplicating.
- CSRF token is generated server-side and exposed through `/auth/me`, then
  attached to every mutating request as `X-CSRF-Token`.
- `PermissionGate` hides admin-only UI but is never the security boundary -
  the backend must independently reject unauthorized requests regardless of
  what the frontend renders (see
  [plan.md - Shared contract C2](/docs/roadmap/0002-admin_console/plan.md)).
- Login success, login failure, and logout are audit-logged events (see
  [plan.md - Shared contract C7](/docs/roadmap/0002-admin_console/plan.md)).
- This task gates every other task in the milestone: nothing else is
  reachable until login and the protected-route shell work.
