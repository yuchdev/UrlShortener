# 01 - Frontend Test Infrastructure

**Parent task:** 09.0 Tests, CI, and Documentation
**State:** ⬜ Not started
**Depends on:** none (consolidates infrastructure introduced piecemeal by
tasks 01.0 and 03.0-08.0's test subtasks)
**Blocks:** none

## Objective

Finalize the shared Vitest/Playwright configuration and package scripts, and
add the full e2e smoke suite covering login, dashboard, permissions, and URL
pairs end to end.

## Files to add

```text
web/admin/vitest.config.ts
web/admin/playwright.config.ts
web/admin/src/test/setupTests.ts
web/admin/src/test/msw/handlers.ts
web/admin/src/test/msw/server.ts
web/admin/e2e/login.spec.ts
web/admin/e2e/dashboard.spec.ts
web/admin/e2e/permissions.spec.ts
web/admin/e2e/url-pairs.spec.ts
```

`web/admin/src/test/msw/handlers.ts`/`server.ts` and `e2e/login.spec.ts`
already exist from task 01.0 subtask 04 - this subtask consolidates them
into the final shared config and extends `handlers.ts` with every endpoint
added by tasks 02.0-08.0, rather than recreating them.

## Package scripts

`web/admin/package.json` must include:

```json
{
  "scripts": {
    "dev": "vite",
    "build": "tsc -b && vite build",
    "preview": "vite preview",
    "lint": "eslint .",
    "typecheck": "tsc -b --noEmit",
    "test": "vitest run",
    "test:watch": "vitest",
    "e2e": "playwright test"
  }
}
```

## Requirements

1. `vitest.config.ts` wires `setupTests.ts` (jsdom environment, testing-
   library matchers, MSW server lifecycle hooks).
2. `playwright.config.ts` targets the built/served admin app (or the Vite
   dev server) with a reasonable default browser project.
3. `dashboard.spec.ts` covers: login -> land on `/admin/dashboard` -> see
   populated (or correctly empty) metrics.
4. `permissions.spec.ts` covers: read-only login -> Console Users nav item
   absent -> attempting a direct navigation to `/admin/console-users` is
   blocked or shows a not-authorized state.
5. `url-pairs.spec.ts` covers: navigate to `/admin/url-pairs` -> paginate ->
   (as admin) open the disable confirmation dialog.
6. `handlers.ts` must cover every endpoint introduced across tasks 01.0-08.0
   by the time this subtask is considered done - audit the full endpoint
   list in
   [04-admin-api-contract.md](/docs/roadmap/0002-admin_console/04-admin-api-contract.md)
   against it.

## Constraints

- e2e specs are smoke tests, not exhaustive coverage - deep functional
  coverage lives in each task's own frontend unit tests (already written in
  tasks 01.0-08.0's test subtasks).

## Success criteria

- [ ] `npm run test` runs the full Vitest suite from every task.
- [ ] `npm run e2e` runs all four e2e specs headless.
- [ ] `handlers.ts` has a mock handler for every `/admin/api/v1/*` endpoint
      in the API contract.
- [ ] All four package scripts (`build`, `lint`, `typecheck`, `test`, `e2e`)
      succeed against the finished codebase.
