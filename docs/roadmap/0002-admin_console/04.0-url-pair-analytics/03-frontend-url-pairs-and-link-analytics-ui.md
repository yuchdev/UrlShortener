# 03 - Frontend URL Pairs and Link Analytics UI

**Parent task:** 04.0 URL Pair List and URL Pair Analytics
**State:** ⬜ Not started
**Depends on:** 01, 02
**Blocks:** 04

## Objective

Build `UrlPairsPage`, `LinkAnalyticsPage`, and `LinkDetailPage`, including
the admin-only disable action with confirmation dialog.

## Files to add

```text
web/admin/src/api/urlPairsApi.ts
web/admin/src/model/urlPair.ts
web/admin/src/pages/UrlPairsPage.tsx
web/admin/src/pages/LinkAnalyticsPage.tsx
web/admin/src/pages/LinkDetailPage.tsx
web/admin/src/components/analytics/UrlPairSummaryCard.tsx
web/admin/src/components/analytics/ReferrerTable.tsx
web/admin/src/components/analytics/RecentRedirectEventsTable.tsx
```

## Requirements

1. `UrlPairsPage` (`/admin/url-pairs`) uses `PaginatedTable` (task 02.0)
   with columns `Short Code, Target URL, Created At, Updated At, Status,
   Redirect Count, Unique Visitors, Last Redirect, Actions` (
   [01-product-and-ux-spec.md §8.1](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)).
2. Row actions: View analytics, Copy short URL, Open target, Disable
   (admin-only, `url_pairs:write` via `PermissionGate`), Edit (if backend
   supports it), Delete (only if backend supports safe deletion - omit if
   subtask 01 did not implement it).
3. `LinkAnalyticsPage` (`/admin/analytics/links`) uses `PaginatedTable` with
   columns `Short Code, Target URL, Created At, Redirects, Unique Visitors,
   Unique IPs, Unique Fingerprints, Last Redirect, Suspicious %, Status`.
4. `LinkDetailPage` (`/admin/analytics/links/{url_pair_id}`) required
   sections (
   [01-product-and-ux-spec.md §7.2](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)):

   ```text
   identity header, metric cards, redirect timeline, referrers table,
   visitor profiles table, recent redirect events, suspicious events
   ```

   built from `UrlPairSummaryCard`, `MetricCard`/`TimeSeriesChart` (task
   03.0), `ReferrerTable`, and `RecentRedirectEventsTable`.
5. Disable action: confirmation dialog, CSRF token attached automatically by
   `apiClient` (task 01.0), only rendered for `url_pairs:write` via
   `PermissionGate`, and invalidates the `['urlPairs', ...]` query on
   success.
6. Query keys: `['urlPairs', filters, pagination]`,
   `['analytics', 'links', filters, pagination]` (
   [02-react-frontend-architecture.md §5](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md)).

## Constraints

- Backend is authoritative for the disable action - the frontend
  confirmation dialog and `PermissionGate` are UX only, per
  [plan.md - Shared contract C2](/docs/roadmap/0002-admin_console/plan.md).

## Success criteria

- [ ] URL pairs list is paginated and searchable by short code/target
      domain.
- [ ] Link analytics detail page renders real data from all four
      sub-resource endpoints.
- [ ] Admin sees and can use the disable action; read-only user does not see
      it.
- [ ] Disable action shows a confirmation dialog before submitting.
