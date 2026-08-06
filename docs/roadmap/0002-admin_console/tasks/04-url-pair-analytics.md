# Task 04 — URL Pair List and URL Pair Analytics

## Goal

Implement paginated URL pair browsing and per-link analytics detail pages.

## Required frontend files

Create:

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

## Required backend files

Create:

```text
src/admin/controllers/UrlPairsAdminController.h
src/admin/controllers/UrlPairsAdminController.cpp
src/admin/controllers/LinkAnalyticsController.h
src/admin/controllers/LinkAnalyticsController.cpp
src/admin/storage/UrlPairsAdminRepository.h
src/admin/storage/UrlPairsAdminRepository.cpp
src/admin/analytics/LinkAnalyticsService.h
src/admin/analytics/LinkAnalyticsService.cpp
src/admin/dto/UrlPairAdminDto.h
src/admin/dto/UrlPairAdminDto.cpp
src/admin/dto/LinkAnalyticsDto.h
src/admin/dto/LinkAnalyticsDto.cpp
```

## Required endpoints

```http
GET /admin/api/v1/url-pairs
GET /admin/api/v1/url-pairs/{url_pair_id}
PATCH /admin/api/v1/url-pairs/{url_pair_id}
GET /admin/api/v1/analytics/links
GET /admin/api/v1/analytics/links/{url_pair_id}
GET /admin/api/v1/analytics/links/{url_pair_id}/timeseries
GET /admin/api/v1/analytics/links/{url_pair_id}/referrers
GET /admin/api/v1/analytics/links/{url_pair_id}/visitors
GET /admin/api/v1/analytics/links/{url_pair_id}/events
```

## URL pairs table columns

```text
Short Code
Target URL
Created At
Updated At
Status
Redirect Count
Unique Visitors
Last Redirect
Actions
```

## Link analytics columns

```text
Short Code
Target URL
Created At
Redirects
Unique Visitors
Unique IPs
Unique Fingerprints
Last Redirect
Suspicious %
Status
```

## Link detail page

Required sections:

```text
identity header
metric cards
redirect timeline
referrers table
visitor profiles table
recent redirect events
suspicious events
```

## Admin-only actions

Initial write action:

```text
disable URL pair
```

Rules:

- visible only to users with `url_pairs:write`;
- requires confirmation dialog;
- requires CSRF token;
- writes audit log event;
- read-only users must be rejected by backend.

## Tests to add

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

## Exit criteria

- URL pairs are paginated.
- URL pairs can be searched by short code and target domain.
- Link analytics detail page shows real data.
- Admin can disable URL pair if backend supports status.
- Read-only user cannot disable URL pair.
- Disable action is audit-logged.
