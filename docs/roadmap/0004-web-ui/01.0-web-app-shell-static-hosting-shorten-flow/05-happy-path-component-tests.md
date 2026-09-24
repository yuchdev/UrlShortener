# 05 - Happy-Path Component Tests

**Parent task:** 01.0 Web App Shell, Static Hosting, and Shorten Flow
**State:** ⬜ Not started
**Depends on:** 04
**Blocks:** none

## Objective

Add Vitest component tests covering the shorten happy path, with the backend
mocked via MSW. This subtask stands up a *local* test setup sufficient to run
these tests; task 04.0 consolidates and dedupes test infra across the
milestone.

## Files to add

```text
web/app/vitest.config.ts
web/app/src/test/setupTests.ts
web/app/src/test/msw/server.ts
web/app/src/test/msw/handlers.ts
web/app/src/test/fixtures/linkView.ts
web/app/src/components/shorten/ShortenForm.test.tsx
web/app/src/components/shorten/ShortenResultCard.test.tsx
```

## Requirements

1. Configure Vitest (jsdom environment) and MSW so component tests can render
   the shorten flow against a mocked `POST /api/v1/links`.
2. `handlers.ts` provides a default success handler returning the verified
   create response shape (from the fixture), reusing the Zod-schema field
   names from subtask 03.
3. Happy-path tests must cover, at minimum:
   - rendering the form with URL and optional-slug inputs;
   - submitting a valid URL and asserting the loading state, then the result
     card with the returned `short_url`;
   - the copy button invoking the clipboard path and showing the "Copied"
     confirmation (clipboard mocked);
   - submitting with a custom slug and asserting it is sent in the request
     body.
4. Keep fault-mode tests out of this subtask — they belong to task 02.0
   subtask 04. Only obviously-invalid *client-side* validation (empty URL)
   may be asserted here as part of the happy-path form contract.

## Constraints

- MSW mocks the network; tests never hit a real backend.
- The MSW setup here may be minimal/local; task 04.0 owns consolidation, so
  avoid over-engineering shared harness code in this subtask.
- Follow the frontend test conventions that task 04.0 will formalize (naming,
  fixture location) closely enough that consolidation is mechanical.

## Success criteria

- [ ] `cd web/app && npm run test` runs the Vitest suite green.
- [ ] The shorten happy path (submit → loading → result card → copy) is
      covered end to end against MSW.
- [ ] A custom-slug submission asserts the slug reaches the request body.
- [ ] No fault-mode assertions live here (deferred to task 02.0).
