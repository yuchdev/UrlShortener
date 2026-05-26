# Task 01 — React Admin Shell and Safe Login Integration

## Goal

Create the React admin application shell and connect it to the existing safe login system.

This task must make `/admin` usable but does not need full analytics yet.

## Required frontend files

Create:

```text
web/admin/package.json
web/admin/tsconfig.json
web/admin/vite.config.ts
web/admin/index.html
web/admin/src/main.tsx
web/admin/src/app/App.tsx
web/admin/src/app/router.tsx
web/admin/src/app/providers.tsx
web/admin/src/app/config.ts
web/admin/src/auth/AuthProvider.tsx
web/admin/src/auth/ProtectedRoute.tsx
web/admin/src/auth/PermissionGate.tsx
web/admin/src/auth/authTypes.ts
web/admin/src/auth/useAuth.ts
web/admin/src/api/apiClient.ts
web/admin/src/api/authApi.ts
web/admin/src/model/auth.ts
web/admin/src/components/layout/AdminLayout.tsx
web/admin/src/components/layout/Sidebar.tsx
web/admin/src/components/layout/TopBar.tsx
web/admin/src/components/ui/Button.tsx
web/admin/src/components/ui/Card.tsx
web/admin/src/components/ui/Input.tsx
web/admin/src/components/ui/Spinner.tsx
web/admin/src/components/ui/EmptyState.tsx
web/admin/src/components/ui/ErrorState.tsx
web/admin/src/pages/LoginPage.tsx
web/admin/src/pages/DashboardPage.tsx
web/admin/src/pages/SettingsPage.tsx
```

## Required backend files

Create or extend according to existing project layout:

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

If equivalent files already exist from the safe-login task, extend them instead of duplicating.

## Required API endpoints

```http
POST /admin/api/v1/auth/login
POST /admin/api/v1/auth/logout
GET  /admin/api/v1/auth/me
```

## Frontend requirements

1. Use React + TypeScript + Vite.
2. Use TanStack Router.
3. Use TanStack Query.
4. Store auth state only through server session and `/auth/me`.
5. Do not store auth tokens in localStorage.
6. Login page must show validation errors.
7. Protected routes must redirect anonymous users to `/admin/login`.
8. Sidebar must show the main navigation structure.
9. Top bar must show username, role, environment, and logout.
10. `PermissionGate` must hide admin-only controls from read-only users.

## Backend requirements

1. Reuse the safe login verifier from the previous task.
2. Use server-side sessions.
3. Set secure cookie flags.
4. Generate and expose CSRF token through `/auth/me`.
5. Reject unauthenticated admin API requests.
6. Reject unauthorized requests based on role/permissions.
7. Write audit events for login success, login failure, and logout.

## Tests to add

Frontend:

```text
web/admin/src/pages/LoginPage.test.tsx
web/admin/src/auth/ProtectedRoute.test.tsx
web/admin/src/auth/PermissionGate.test.tsx
web/admin/src/test/msw/handlers.ts
web/admin/src/test/msw/server.ts
```

Backend:

```text
tests/unit/admin/AdminSessionMiddlewareTest.cpp
tests/unit/admin/AdminPermissionMiddlewareTest.cpp
tests/integration/admin/AdminAuthApiTest.cpp
```

E2E:

```text
web/admin/e2e/login.spec.ts
```

## Exit criteria

- `npm run build` succeeds in `web/admin`.
- Anonymous user cannot access `/admin/dashboard`.
- Admin user can log in and see dashboard shell.
- Read-only user can log in and see dashboard shell.
- Read-only user does not see Console Users navigation item.
- Backend rejects unauthenticated admin API calls.
- Backend rejects read-only calls to admin-only endpoints.
- Login/logout events appear in audit log storage.
