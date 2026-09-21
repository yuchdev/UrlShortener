# 04 - Frontend Explorer Pages

**Parent task:** 05.0 Visitor, Fingerprint, and IP Explorer
**State:** ⬜ Not started
**Depends on:** 01, 02, 03
**Blocks:** 05

## Objective

Build the list and detail pages for visitor profiles, fingerprints, and IP
addresses, composing `PaginatedTable` (task 02.0) with the privacy
primitives (subtask 03) against the backend APIs (subtasks 01-02).

## Files to add

```text
web/admin/src/api/visitorProfilesApi.ts
web/admin/src/api/fingerprintsApi.ts
web/admin/src/api/ipsApi.ts
web/admin/src/model/visitorProfile.ts
web/admin/src/model/fingerprint.ts
web/admin/src/model/ipAddress.ts
web/admin/src/pages/VisitorProfilesPage.tsx
web/admin/src/pages/VisitorProfileDetailPage.tsx
web/admin/src/pages/FingerprintsPage.tsx
web/admin/src/pages/FingerprintDetailPage.tsx
web/admin/src/pages/IpAddressesPage.tsx
web/admin/src/pages/IpAddressDetailPage.tsx
```

## Requirements

1. Three list pages at `/admin/visitor-profiles`, `/admin/fingerprints`,
   `/admin/ips` using `PaginatedTable` with the column sets from subtasks
   01-02.
2. Three detail pages at `/admin/visitor-profiles/{id}`,
   `/admin/fingerprints/{id}`, `/admin/ips/{id}`, each with: summary cards,
   risk score + `RiskReasonsList`, related objects, `IdentityTimeline`,
   recent redirect events, suspicious events (
   [01-product-and-ux-spec.md §7.3-7.5](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)).
3. `FingerprintDetailPage` additionally renders `FingerprintSignalsPanel`,
   provider + provider confidence, and regeneration history.
4. `IpAddressDetailPage` additionally renders `MaskedIp`-wrapped IP display,
   associated fingerprints/visitor profiles, user-agent distribution, and
   rate-limit events.
5. Query keys: `['visitorProfiles', filters, pagination]`,
   `['fingerprints', filters, pagination]`, `['ips', filters, pagination]` (
   [02-react-frontend-architecture.md §5](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md)).
6. Every raw/sensitive field rendered on these pages must go through
   `MaskedIp`/`FingerprintId`/`SensitiveValue` - no page renders a raw IP or
   fingerprint field directly.

## Constraints

- Do not add a UI affordance to reveal raw fingerprint components even
  behind a client-side toggle - that would violate
  [plan.md - Shared contract C5](/docs/roadmap/0002-admin_console/plan.md);
  any future debug-mode reveal is a backend-gated, explicitly out-of-scope
  concern for this milestone.

## Success criteria

- [ ] Visitor profiles, fingerprints, and IP addresses are paginated and
      searchable.
- [ ] Detail pages show related objects and recent events correctly.
- [ ] Read-only users see masked IPs on every page that displays one.
- [ ] Raw fingerprint components are not exposed anywhere in the UI.
