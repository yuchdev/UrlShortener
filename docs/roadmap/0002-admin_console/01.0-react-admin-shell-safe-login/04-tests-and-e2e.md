# 04 - Tests and E2E

**Parent task:** 01.0 React Admin Shell and Safe Login Integration
**State:** ⬜ Not started
**Depends on:** 03
**Blocks:** none

## Objective

Cover the app shell and auth integration with frontend unit tests, backend
unit/integration tests, and an end-to-end login flow, plus the MSW mock
server infrastructure that every later frontend test subtask reuses.

## Files to add

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

## Requirements

1. `web/admin/src/test/msw/handlers.ts` + `server.ts` stand up a mocked
   `/admin/api/v1/auth/*` surface reusable by every later frontend test
   subtask - build it generically (a base handler list plus per-test
   overrides), not login-specific only.
2. `LoginPage.test.tsx` covers: empty-field validation, wrong-credential
   error rendering, and successful-login redirect.
3. `ProtectedRoute.test.tsx` covers all three auth states (`checking`,
   `anonymous`, `authenticated`).
4. `PermissionGate.test.tsx` covers hidden-vs-visible rendering for a
   permission the current mocked user does and does not have.
5. Backend unit tests exercise `AdminSessionMiddleware` and
   `AdminPermissionMiddleware` directly (no network), covering accept/reject
   branches enumerated in subtask 02.
6. `AdminAuthApiTest` is a real integration test against the running admin
   router: login success, login failure, authenticated `/auth/me`, anonymous
   `/auth/me`, logout.
7. `login.spec.ts` is a Playwright e2e test: unauthenticated visit to
   `/admin/dashboard` redirects to `/admin/login`; valid login lands on
   `/admin/dashboard` and shows the shell.

## Constraints

- Unit tests must not depend on network access; only the e2e spec drives a
  real (or fully mocked) backend.
- Keep MSW handlers additive - later subtasks (e.g. 02.0/04) extend
  `handlers.ts` rather than replacing it.

## Success criteria

- [ ] All frontend unit tests above pass under `npm run test`.
- [ ] All backend unit/integration tests above pass under the project's CTest
      run.
- [ ] `login.spec.ts` passes headless in CI-equivalent mode.
- [ ] Anonymous user cannot access `/admin/dashboard` (asserted by both a
      frontend unit test and the e2e spec).
- [ ] Backend rejects unauthenticated and unauthorized admin API calls
      (asserted by integration tests).
- [ ] Read-only user can log in and see the dashboard shell but not the
      Console Users nav item (asserted by a frontend test).
