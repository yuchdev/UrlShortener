# 03 - Frontend Auth Integration

**Parent task:** 01.0 React Admin Shell and Safe Login Integration
**State:** ⬜ Not started
**Depends on:** 01, 02
**Blocks:** 04

## Objective

Wire the React shell (subtask 01) to the backend auth endpoints (subtask 02):
the `AuthProvider`/`ProtectedRoute`/`PermissionGate` trio, the API client,
the login page, and role-aware navigation/top-bar rendering.

## Files to add

```text
web/admin/src/auth/AuthProvider.tsx
web/admin/src/auth/ProtectedRoute.tsx
web/admin/src/auth/PermissionGate.tsx
web/admin/src/auth/authTypes.ts
web/admin/src/auth/useAuth.ts
web/admin/src/api/apiClient.ts
web/admin/src/api/authApi.ts
web/admin/src/model/auth.ts
web/admin/src/pages/LoginPage.tsx
```

## Requirements

1. `AuthProvider` loads `GET /admin/api/v1/auth/me` on startup and exposes
   auth state `checking | authenticated | anonymous | error` (
   [02-react-frontend-architecture.md §4](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md)).
2. `ProtectedRoute` behavior: `checking` -> app loader; `anonymous` ->
   redirect to `/admin/login`; `authenticated` -> render the route.
3. `PermissionGate` hides controls the current user's `permissions[]` does
   not include; it must not be relied on for security (see
   [plan.md - Shared contract C2](/docs/roadmap/0002-admin_console/plan.md)).
   Use it to hide the Console Users nav item for read-only users.
4. `apiClient.ts` sends every request with `credentials: 'include'` and
   attaches `X-CSRF-Token` from the auth state on mutating requests; it
   parses `{error:{code,message,request_id}}` responses into typed frontend
   errors (
   [plan.md - Shared contract C4](/docs/roadmap/0002-admin_console/plan.md)).
5. Auth state is held only in memory/React Query cache backed by the server
   session - never write tokens to `localStorage`.
6. `LoginPage` shows field-level and request-level validation errors and
   redirects to `/admin/dashboard` (never `/admin/url-pairs`) on success (
   [01-product-and-ux-spec.md §5](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)).
7. Wire `TopBar` (from subtask 01) to show the real username, role badge,
   and a working logout button that calls `POST /auth/logout` and clears
   query cache.

## Constraints

- `auth/me` query key is `['auth', 'me']` with a 60s stale time (
  [02-react-frontend-architecture.md §5](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md)).
- Do not hand-roll fetch calls outside `apiClient.ts`/`authApi.ts` - every
  later API module builds on this client.

## Success criteria

- [ ] Anonymous user hitting `/admin/dashboard` is redirected to
      `/admin/login`.
- [ ] Successful login redirects to `/admin/dashboard`.
- [ ] `LoginPage` renders validation errors for bad credentials.
- [ ] Read-only user does not see the Console Users nav item.
- [ ] Logout clears session and returns the user to `/admin/login`.
- [ ] CSRF token from `/auth/me` is attached to a mutating request (verified
      against a mocked API in a later test subtask).
