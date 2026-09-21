# 02 - Backend Session Auth

**Parent task:** 01.0 React Admin Shell and Safe Login Integration
**State:** ⬜ Not started
**Depends on:** none
**Blocks:** 03

## Objective

Implement the backend half of the admin auth boundary: the admin HTTP router
and static-asset handler, session/permission middleware, and the auth
controller backing `/admin/api/v1/auth/*`, reusing the safe-login verifier
built in the prior safe-login task.

## Files to add

Create or extend (if equivalent files already exist from the safe-login
task, extend them instead of duplicating):

```text
src/admin/http/AdminRouter.h
src/admin/http/AdminRouter.cpp
src/admin/http/AdminStaticAssetsHandler.h
src/admin/http/AdminStaticAssetsHandler.cpp
src/admin/auth/AdminSessionMiddleware.h
src/admin/auth/AdminSessionMiddleware.cpp
src/admin/auth/AdminPermissionMiddleware.h
src/admin/auth/AdminPermissionMiddleware.cpp
src/admin/controllers/AdminAuthController.h
src/admin/controllers/AdminAuthController.cpp
src/admin/model/AdminUserDto.h
src/admin/model/AdminUserDto.cpp
```

## API contract

```http
POST /admin/api/v1/auth/login
POST /admin/api/v1/auth/logout
GET  /admin/api/v1/auth/me
```

`GET /auth/me` response shape (see
[04-admin-api-contract.md §3](/docs/roadmap/0002-admin_console/04-admin-api-contract.md)):

```json
{
  "username": "admin",
  "role": "admin",
  "permissions": ["analytics:read", "url_pairs:read", "..."],
  "csrf_token": "..."
}
```

## Requirements

1. Reuse the safe-login verifier from the previous safe-login task - do not
   reimplement credential checking.
2. Use server-side sessions; set `HttpOnly`, `Secure` (in HTTPS), and
   `SameSite=Lax` or `Strict` cookie flags.
3. Generate a CSRF token per session and expose it through `/auth/me`;
   `AdminPermissionMiddleware` must reject mutating requests (`POST`,
   `PATCH`, `PUT`, `DELETE`) missing a valid `X-CSRF-Token`.
4. `AdminSessionMiddleware` rejects unauthenticated requests to any
   `/admin/api/v1/*` route except `POST /auth/login` with `not_authenticated`
   in the common error format (
   [plan.md - Shared contract C4](/docs/roadmap/0002-admin_console/plan.md)).
5. `AdminPermissionMiddleware` rejects requests from users lacking the
   required permission with `not_authorized`.
6. `AdminStaticAssetsHandler` serves `web/admin/dist/` under `/admin` so the
   built React app is reachable without a separate frontend server in
   production.
7. Write audit events (via the audit logging path - `AuditLogger` itself is
   built in task 07.0, so this subtask may write directly to the storage
   backend or stub the call behind an interface task 07.0 will implement)
   for login success, login failure, and logout, per
   [plan.md - Shared contract C7](/docs/roadmap/0002-admin_console/plan.md).

## Constraints

- No browser-side password hash exposure and no direct browser access to the
  credential database - `AdminUserDto` must never carry a password hash.
- Keep CSRF/session logic in the middleware layer, not scattered across
  controllers.

## Success criteria

- [ ] `AdminSessionMiddlewareTest` covers accept/reject for authenticated,
      anonymous, and expired-session requests.
- [ ] `AdminPermissionMiddlewareTest` covers permission-granted,
      permission-denied, and missing-CSRF-token cases.
- [ ] `AdminAuthApiTest` (integration) covers login success, login failure,
      `GET /auth/me` while authenticated and anonymous, and logout.
- [ ] Login/logout events are recorded wherever audit storage currently
      lands.
- [ ] Static asset handler serves `web/admin/dist/` under `/admin` when the
      build output exists.
