# 04 - Tests

**Parent task:** 04.0 URL Pair List and URL Pair Analytics
**State:** ⬜ Not started
**Depends on:** 03
**Blocks:** none

## Objective

Cover URL pair CRUD/disable, link analytics endpoints, and the frontend
pages, explicitly including the read-only-cannot-disable permission case.

## Files to add

Frontend:

```text
web/admin/src/pages/UrlPairsPage.test.tsx
web/admin/src/pages/LinkDetailPage.test.tsx
web/admin/src/components/analytics/RecentRedirectEventsTable.test.tsx
```

Backend:

```text
tests/unit/admin/UrlPairsAdminRepositoryTest.cpp
tests/unit/admin/LinkAnalyticsServiceTest.cpp
tests/integration/admin/UrlPairsAdminApiTest.cpp
tests/integration/admin/LinkAnalyticsApiTest.cpp
tests/integration/admin/ReadOnlyCannotDisableUrlPairTest.cpp
```

## Requirements

1. `UrlPairsAdminRepositoryTest` / `LinkAnalyticsServiceTest` are unit tests
   against fixture data for aggregation correctness (counts, last-redirect
   timestamps, suspicious percentage).
2. `UrlPairsAdminApiTest` / `LinkAnalyticsApiTest` are integration tests
   against the real admin router with seeded redirect events.
3. `ReadOnlyCannotDisableUrlPairTest` is a dedicated integration test
   asserting a read-only-role session receives `not_authorized` (not a
   silent no-op) when calling `PATCH /url-pairs/{id}` to disable.
4. `UrlPairsPage.test.tsx` covers rendering, search, and that the disable
   action is hidden for a read-only mocked user.
5. `LinkDetailPage.test.tsx` covers all required sections rendering from
   mocked sub-resource responses.
6. `RecentRedirectEventsTable.test.tsx` covers empty and populated states.

## Constraints

- Keep the read-only-permission-denial test isolated in its own file
  (matching the source task's explicit test list) so CI surfaces a
  permission regression distinctly from a functional regression.

## Success criteria

- [ ] All backend tests above pass under CTest.
- [ ] All frontend tests above pass under `npm run test`.
- [ ] Read-only user cannot disable a URL pair (backend-enforced, asserted
      by integration test).
- [ ] Disable action is confirmed as audit-logged by at least one
      integration test assertion.
