# 05 - Tests

**Parent task:** 05.0 Visitor, Fingerprint, and IP Explorer
**State:** ⬜ Not started
**Depends on:** 04
**Blocks:** none

## Objective

Cover the visitor/fingerprint/IP repositories and APIs, the explorer pages,
and - explicitly - the read-only IP masking behavior called out in the
source task's exit criteria.

## Files to add

Frontend:

```text
web/admin/src/pages/VisitorProfilesPage.test.tsx
web/admin/src/pages/FingerprintsPage.test.tsx
web/admin/src/pages/IpAddressesPage.test.tsx
web/admin/src/components/privacy/MaskedIp.test.tsx
web/admin/src/components/analytics/FingerprintSignalsPanel.test.tsx
```

Backend:

```text
tests/unit/admin/VisitorProfilesRepositoryTest.cpp
tests/unit/admin/FingerprintsRepositoryTest.cpp
tests/unit/admin/IpAddressesRepositoryTest.cpp
tests/integration/admin/VisitorProfilesApiTest.cpp
tests/integration/admin/FingerprintsApiTest.cpp
tests/integration/admin/IpAddressesApiTest.cpp
tests/integration/admin/ReadOnlyIpMaskingTest.cpp
```

## Requirements

1. Repository unit tests cover aggregation correctness against fixture data
   for each of the three identity types.
2. API integration tests cover list/detail/sub-resource endpoints against
   the real admin router with seeded data.
3. `ReadOnlyIpMaskingTest` is a dedicated integration test asserting a
   read-only session receives masked IPs and an admin-with-`ips:read_full`
   session receives full IPs (when storage policy allows), for both the IP
   list and IP detail endpoints.
4. Frontend page tests cover pagination/search rendering against MSW-mocked
   data (extending `web/admin/src/test/msw/handlers.ts`).
5. `MaskedIp.test.tsx` and `FingerprintSignalsPanel.test.tsx` are the
   focused component tests already specified in subtask 03's success
   criteria, re-run here as part of the task-level suite.

## Constraints

- Keep the masking test isolated in its own file, matching the source
  task's explicit test list, so a masking regression is distinguishable in
  CI from a general API regression.

## Success criteria

- [ ] All backend tests above pass under CTest.
- [ ] All frontend tests above pass under `npm run test`.
- [ ] `ReadOnlyIpMaskingTest` passes for both masked and full-IP cases.
- [ ] Backend tests verify masking behavior end-to-end (not just at the DTO
      layer).
