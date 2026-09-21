# 02 - Backend Link Analytics

**Parent task:** 04.0 URL Pair List and URL Pair Analytics
**State:** ⬜ Not started
**Depends on:** none (parallel with subtask 01; both build on task 02.0)
**Blocks:** 03

## Objective

Implement the per-link analytics endpoints backing `/admin/analytics/links`
and the link detail page: aggregate columns, timeseries, referrers,
visitors, and recent events for a single URL pair.

## Files to add

```text
src/admin/controllers/LinkAnalyticsController.h
src/admin/controllers/LinkAnalyticsController.cpp
src/admin/analytics/LinkAnalyticsService.h
src/admin/analytics/LinkAnalyticsService.cpp
src/admin/dto/LinkAnalyticsDto.h
src/admin/dto/LinkAnalyticsDto.cpp
```

## API contract

```http
GET /admin/api/v1/analytics/links
GET /admin/api/v1/analytics/links/{url_pair_id}
GET /admin/api/v1/analytics/links/{url_pair_id}/timeseries
GET /admin/api/v1/analytics/links/{url_pair_id}/referrers
GET /admin/api/v1/analytics/links/{url_pair_id}/visitors
GET /admin/api/v1/analytics/links/{url_pair_id}/events
```

## Requirements

1. `GET /analytics/links` list columns (
   [01-product-and-ux-spec.md §7.2](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)):

   ```text
   Short Code, Target URL, Created At, Redirects, Unique Visitors,
   Unique IPs, Unique Fingerprints, Last Redirect, Suspicious %, Status
   ```

2. `GET /analytics/links/{url_pair_id}` returns the detail summary:
   short code, target URL, status, created/updated timestamps, total
   redirects, unique visitors/fingerprints/IPs (
   [01-product-and-ux-spec.md §7.2](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)).
3. `/timeseries` returns the redirect-volume-over-time series for the link;
   `/referrers` returns per-link referrer breakdown; `/visitors` returns
   associated visitor profiles; `/events` returns recent redirect events
   (paginated via task 02.0 primitives).
4. `LinkAnalyticsService` queries the immutable redirect-event stream or
   `analytics_redirects_by_url_pair` aggregate (
   [05-data-model-and-storage.md §8](/docs/roadmap/0002-admin_console/05-data-model-and-storage.md))
   using the `redirect_event(url_pair_id, timestamp)` index.
5. `Suspicious %` and per-event `risk_score`/`risk_level` fields are wired
   from `redirect_event.risk_score`/`risk_level`; they will read as `0`/
   `low` until task 06.0's `SuspicionAnalyzer` populates real risk data -
   compute the percentage correctly against whatever is present rather than
   hardcoding.
6. Enforce `analytics:read`.

## Constraints

- Reuse `TimeRange`/`PageRequest` from task 02.0 for every one of these
  endpoints - no bespoke pagination.

## Success criteria

- [ ] `GET /analytics/links` returns the documented columns, paginated and
      filterable.
- [ ] `GET /analytics/links/{id}` and its four sub-resources return correct
      data for a seeded URL pair.
- [ ] `Suspicious %` computes correctly against zero and non-zero risk data.
- [ ] `analytics:read` is enforced on every endpoint in this subtask.
