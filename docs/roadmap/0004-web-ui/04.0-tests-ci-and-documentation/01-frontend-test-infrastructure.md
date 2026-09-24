# 01 - Frontend Test Infrastructure

**Parent task:** 04.0 Tests, CI, and Documentation
**State:** ⬜ Not started
**Depends on:** 01.0-03.0
**Blocks:** 02

## Objective

Consolidate the Vitest + MSW setup introduced locally in tasks 01.0-03.0 into
one coherent, deduplicated test infrastructure, and stand up the Playwright
config so the e2e suite (subtask 02) has a home.

## Files to add / edit

```text
web/app/vitest.config.ts               # finalize (edit)
web/app/playwright.config.ts           # add
web/app/src/test/setupTests.ts         # consolidate (edit)
web/app/src/test/msw/server.ts         # consolidate (edit)
web/app/src/test/msw/handlers.ts       # single source of default handlers (edit)
web/app/src/test/msw/faultHandlers.ts  # shared fault catalogue (edit)
web/app/src/test/fixtures/*            # consolidate fixtures (edit)
web/app/package.json                   # finalize test/e2e scripts (edit)
```

## Requirements

1. Deduplicate the MSW server/handlers/fixtures that tasks 01.0-03.0 created
   locally into a single shared module set; component tests import from there
   rather than redefining handlers.
2. Finalize `package.json` scripts: `dev`, `build`, `preview`, `lint`,
   `typecheck`, `test` (Vitest), and `test:e2e` (Playwright).
3. Add `playwright.config.ts` targeting a locally served build (either
   `vite preview` of `web/app/dist/` or the C++ backend serving `/app`) so the
   e2e suite in subtask 02 can run against the real static-hosting path.
4. Ensure the fault catalogue in `faultHandlers.ts` is the single source both
   component tests and e2e mirror, so the "every fault mode" requirement is
   traceable to one list.
5. No behavior/feature changes — this is infrastructure consolidation only.

## Constraints

- Do not weaken existing per-task tests while moving them onto shared infra;
  they must still pass.
- Keep the shared harness minimal — no speculative test frameworks.

## Success criteria

- [ ] A single shared MSW server/handlers/fixtures module set exists; no
      duplicated handler definitions remain across test files.
- [ ] `npm run test`, `npm run lint`, `npm run typecheck`, and `npm run build`
      all succeed.
- [ ] `playwright.config.ts` exists and can launch the built app under `/app`.
- [ ] The fault catalogue is single-sourced and referenced by both component
      and e2e tests.
