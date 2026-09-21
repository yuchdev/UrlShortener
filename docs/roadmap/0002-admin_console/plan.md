# Milestone 0002 - Admin Console

## Background

The URL shortener currently has no operator-facing surface: there is no way to
see traffic, spot abuse, or browse the URL pairs, visitor identities,
fingerprints, or IP addresses the redirect service already observes. This
milestone builds a React-based web admin console that is **analytics-first**:
tables for URL pairs, fingerprints, IPs, and visitor profiles are required,
but they stay secondary to traffic analytics, abuse detection, identity/
fingerprint exploration, and operational visibility.

The console must answer, at a glance:

```text
What links are being used?
Who or what is using them?
Where does traffic come from?
Which links/fingerprints/IPs look suspicious?
What changed recently?
```

Visitor identity is established through a dual-tier fingerprinting strategy.
Production deployments use **Fingerprint Pro / Fingerprint Identification +
Smart Signals**, selected specifically because the requirement is not merely a
stable `visitorId` but detection of **suspicious or unrealistic fingerprints**
(bot signals, tampering, anti-detect browsers, VPN/proxy/Tor, anomaly
scoring). Browser-only libraries compute their result client-side and are
weaker against spoofing, so local/dev/offline environments fall back to
ThumbmarkJS or FingerprintJS OSS behind the same `FingerprintProvider`
interface, and an internal `SuspicionAnalyzer` always re-scores every
fingerprint against provider signals plus local redirect history regardless
of which provider produced it.

Full rationale, provider comparison, and naming conventions live in
[00-decision-record.md](/docs/roadmap/0002-admin_console/00-decision-record.md).

## Architecture

| Area | Decision |
|---|---|
| Frontend framework | React + TypeScript |
| Build tool | Vite |
| Routing | TanStack Router |
| Server state | TanStack Query |
| Tables | TanStack Table + TanStack Virtual |
| Charts | Apache ECharts via a thin internal wrapper |
| Forms | React Hook Form + Zod |
| Styling | Tailwind CSS + shadcn/ui-style component wrappers |
| Auth model | Existing safe login, server-side session, secure cookies |
| Production fingerprint provider | Fingerprint Pro / Fingerprint Identification + Smart Signals |
| Local/dev fallback fingerprint provider | ThumbmarkJS or FingerprintJS OSS behind the same provider interface |
| Suspicion model | Provider smart signals + internal SuspicionAnalyzer rules |

The frontend lives under `web/admin/` (see
[02-react-frontend-architecture.md](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md)
for the full directory layout, route table, and React Query conventions), and
is served as static assets by the existing C++ backend under `/admin`. Admin
backend code lives under `src/admin/`; the fingerprint-provider and suspicion
layer lives under `src/fingerprint/` and `src/tracking/`, shared with a
public, unauthenticated tracker bundle under `web/public-tracker/` (see
[03-fingerprint-and-suspicion-model.md](/docs/roadmap/0002-admin_console/03-fingerprint-and-suspicion-model.md)).

## Tasks

| Task | Name | Category | Output |
|------|------|----------|--------|
| [01.0](/docs/roadmap/0002-admin_console/01.0-react-admin-shell-safe-login/README.md) | React Admin Shell and Safe Login Integration | Foundation | Usable `/admin` shell wired to the existing safe-login system |
| [02.0](/docs/roadmap/0002-admin_console/02.0-admin-api-query-foundation/README.md) | Admin API Query Foundation | Infrastructure | Shared pagination/filter/sort/time-range primitives and the `PaginatedTable` component used by every later list page |
| [03.0](/docs/roadmap/0002-admin_console/03.0-analytics-dashboard/README.md) | Analytics Dashboard | Analytics | `/admin/dashboard` and `/admin/analytics/overview` backed by real aggregate data |
| [04.0](/docs/roadmap/0002-admin_console/04.0-url-pair-analytics/README.md) | URL Pair List and URL Pair Analytics | Analytics | Paginated URL pair browsing plus per-link analytics detail pages |
| [05.0](/docs/roadmap/0002-admin_console/05.0-visitor-fingerprint-ip-explorer/README.md) | Visitor, Fingerprint, and IP Explorer | Identity | Safe browsing/detail pages for the visitor/fingerprint/IP identity graph |
| [06.0](/docs/roadmap/0002-admin_console/06.0-fingerprint-provider-and-suspicion-analyzer/README.md) | Fingerprint Provider and Suspicion Analyzer | Fingerprinting | `FingerprintProvider` abstraction, normalization, `SuspicionAnalyzer`, public tracking endpoints |
| [07.0](/docs/roadmap/0002-admin_console/07.0-console-users-and-audit-log/README.md) | Console Users and Audit Log | Admin Ops | Console-user management UI/API and a searchable audit log |
| [08.0](/docs/roadmap/0002-admin_console/08.0-exports-privacy-and-permissions/README.md) | Exports, Privacy, and Permissions | Privacy | Controlled data exports and consistent masking/permission enforcement |
| [09.0](/docs/roadmap/0002-admin_console/09.0-tests-ci-and-documentation/README.md) | Tests, CI, and Documentation | Quality | Full test coverage, CI wiring, and `docs/admin-console/` documentation |

## Shared contracts (authoritative)

Cross-cutting rules every task must honor. These are pulled from the
supporting spec docs; extend the source doc, not this list, when a rule
changes.

### C1. Fingerprint provider abstraction boundary

All fingerprint collection goes through a `FingerprintProvider` interface
(`FingerprintProProvider`, `ThumbmarkProvider`, `FingerprintJsOssProvider`,
`NoopFingerprintProvider`). Provider-specific JSON must never leak past the
normalization boundary — every provider result is normalized into
`ClientFingerprintEnvelope` / `FingerprintSignals` before it reaches storage
or the admin API. See
[03-fingerprint-and-suspicion-model.md §6](/docs/roadmap/0002-admin_console/03-fingerprint-and-suspicion-model.md).

### C2. Auth/session model

The admin console reuses the existing safe-login system: server-side
sessions, `HttpOnly` + `Secure` cookies, `SameSite=Lax` or `Strict`, and a
CSRF token issued through `GET /auth/me` and attached to every mutating
request. `PermissionGate` on the frontend hides unavailable actions but is
**not** a security boundary — the backend authorization middleware is
authoritative and must independently reject unauthorized requests. See
[00-decision-record.md §4](/docs/roadmap/0002-admin_console/00-decision-record.md)
and
[06-security-privacy-permissions.md §1](/docs/roadmap/0002-admin_console/06-security-privacy-permissions.md).

### C3. Pagination & query contract

Every list endpoint accepts `page`, `page_size`, `sort_by`, `sort_direction`
(defaults `page=1`, `page_size=50`, capped at `500`) and every analytics
endpoint additionally accepts `from`, `to`, `granularity` (`minute|hour|day`),
`timezone`. Responses use the shared `PageResponse<T>` envelope. All of this
is built once in task 02.0 and reused by every later task. See
[02-react-frontend-architecture.md §3, §6](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md).

### C4. Common API error format

Every `/admin/api/v1/*` error response uses:

```json
{ "error": { "code": "validation_error", "message": "...", "request_id": "req_..." } }
```

with codes drawn from `not_authenticated`, `not_authorized`, `csrf_failed`,
`validation_error`, `not_found`, `conflict`, `rate_limited`, `internal_error`.
See
[04-admin-api-contract.md §2.2](/docs/roadmap/0002-admin_console/04-admin-api-contract.md).

### C5. Privacy / masking rules

The backend decides whether a field is full or masked; the frontend renders
what it receives and must never reconstruct masked data. Raw fingerprint
components (canvas/audio/font/WebGL) are hidden by default for every role.
IPs are masked for read-only users and for admins without `ips:read_full`.
See
[06-security-privacy-permissions.md §5, §6](/docs/roadmap/0002-admin_console/06-security-privacy-permissions.md).

### C6. Roles & permissions model

Two roles (`admin`, `read-only`) plus a fixed permission set (`analytics:read`,
`url_pairs:read`, `url_pairs:write`, `visitor_profiles:read`,
`fingerprints:read`, `ips:read`, `ips:read_full`, `console_users:manage`,
`exports:create_aggregate`, `exports:create_raw`, `audit_log:read`,
`settings:read`, `settings:write`). See
[06-security-privacy-permissions.md §2, §3](/docs/roadmap/0002-admin_console/06-security-privacy-permissions.md).

### C7. Audit logging requirements

Login success/failure, logout, password change, console-user
creation/disable, role change, permission-denied, URL pair mutation, export
request/completion, and settings change are all audit-logged with
`timestamp, console_user, action, target_type, target_id, source_ip_hash,
result, request_id, metadata_json`. See
[06-security-privacy-permissions.md §7](/docs/roadmap/0002-admin_console/06-security-privacy-permissions.md).

### C8. Non-goals for the MVP milestone

- public self-registration
- full visual design polish
- complex RBAC editor UI
- real-time streaming dashboards
- deleting analytics data from the UI
- exposing raw fingerprint components to normal users

## Dependency graph

```text
01.0 React Admin Shell + Safe Login
        |
        v
02.0 Admin API Query Foundation  (pagination, filters, error format)
        |
        +-----------+-----------+-----------+-----------+
        v           v           v           v           v
   03.0 Analytics  04.0 URL   05.0 Visitor/ 06.0 Finger- 07.0 Console
   Dashboard *MVP* Pair       Fingerprint/  print        Users &
                   Analytics  IP Explorer   Provider &   Audit Log
                   *MVP*         ^          Suspicion
                                 |          Analyzer
                                 +-------------+
                                 (risk score / smart signals
                                  feed dashboard + explorer)
        |           |           |             |           |
        +-----------+-----------+-------------+-----------+
                                 |
                                 v
                08.0 Exports, Privacy & Permissions
                (masks/exports data produced by 03.0-07.0)
                                 |
                                 v
                09.0 Tests, CI, and Documentation
                (covers every task above)
```

`01.0` gates everything else (all pages are behind login). `02.0` gates
`03.0`-`08.0` (they all need paginated/filtered queries and the shared error
format). `06.0`'s risk scores and smart signals feed into `03.0`'s dashboard
metrics and `05.0`'s fingerprint/IP detail pages, so those two tasks reach
full fidelity only once `06.0` lands, though their scaffolding does not block
on it.

The first useful milestone — the MVP — is:

```text
React admin shell + safe login + analytics overview + URL pair list + URL pair detail analytics
```

i.e. tasks `01.0 -> 02.0 -> 03.0 (Analytics Overview) -> 04.0 (URL pair list + detail analytics)`.

## File map

```text
web/admin/                                   # React admin console (tasks 01.0-05.0, 07.0-08.0)
  package.json
  tsconfig.json
  vite.config.ts
  index.html
  vitest.config.ts
  playwright.config.ts
  src/
    main.tsx
    app/            App.tsx, router.tsx, providers.tsx, config.ts
    auth/           AuthProvider.tsx, ProtectedRoute.tsx, PermissionGate.tsx, useAuth.ts
    api/            apiClient.ts, authApi.ts, analyticsApi.ts, urlPairsApi.ts,
                     visitorProfilesApi.ts, fingerprintsApi.ts, ipsApi.ts,
                     consoleUsersApi.ts, auditLogApi.ts, exportsApi.ts, schemas.ts
    components/     layout/, ui/, analytics/, tables/, privacy/, admin/, audit/
    pages/          LoginPage, DashboardPage, AnalyticsOverviewPage, LinkAnalyticsPage,
                     LinkDetailPage, UrlPairsPage, VisitorProfilesPage(+Detail),
                     FingerprintsPage(+Detail), IpAddressesPage(+Detail), ReferrersPage,
                     SecurityAnalyticsPage, ConsoleUsersPage, AuditLogPage, SettingsPage
    model/          auth.ts, analytics.ts, urlPair.ts, visitorProfile.ts, fingerprint.ts,
                     ipAddress.ts, auditLog.ts, consoleUser.ts, exportJob.ts,
                     pagination.ts, filters.ts, permissions.ts
    hooks/          useDateRange.ts, usePagination.ts, useSearchParamsState.ts,
                     useDebouncedValue.ts
    test/           msw/handlers.ts, msw/server.ts, fixtures/, setupTests.ts
  e2e/              login.spec.ts, dashboard.spec.ts, permissions.spec.ts, url-pairs.spec.ts

web/public-tracker/                          # Public, unauthenticated fingerprint tracker (task 06.0)
  package.json
  src/
    fingerprint/    FingerprintEnvelope.ts, FingerprintClient.ts, FingerprintProvider.ts,
                     providers/{FingerprintProClient,ThumbmarkClient,FingerprintJsOssClient,
                                NoopFingerprintClient}.ts
    redirect/       enhancedRedirect.ts, sendBeacon.ts

src/admin/                                   # Admin backend (tasks 01.0-02.0, 04.0-08.0)
  http/           AdminRouter.{h,cpp}, AdminStaticAssetsHandler.{h,cpp}
  auth/           AdminSessionMiddleware.{h,cpp}, AdminPermissionMiddleware.{h,cpp}
  query/          PageRequest, PageResponse, SortSpec, TimeRange, AdminFilterParser, AdminQueryError
  controllers/    AdminAuthController, AnalyticsController, UrlPairsAdminController,
                  LinkAnalyticsController, VisitorProfilesController, FingerprintsController,
                  IpAddressesController, ConsoleUsersController, AuditLogController,
                  ExportController
  analytics/      AnalyticsQueryService, AnalyticsOverviewDto, AnalyticsTimeseriesDto,
                  AnalyticsTopListDto, LinkAnalyticsService
  storage/        AnalyticsRepository, UrlPairsAdminRepository, VisitorProfilesRepository,
                  FingerprintsRepository, IpAddressesRepository, ConsoleUsersRepository,
                  AuditLogRepository
  audit/          AuditLogger.{h,cpp}
  export/         ExportService.{h,cpp}, ExportPolicy.{h,cpp}
  privacy/        PrivacyPolicy.{h,cpp}, MaskingService.{h,cpp}
  dto/            PaginationDto, UrlPairAdminDto, LinkAnalyticsDto, VisitorProfileDto,
                  FingerprintDto, IpAddressDto, ConsoleUserDto, AuditLogDto, ExportDto
  model/          AdminUserDto.{h,cpp}

src/fingerprint/                             # Provider abstraction + suspicion engine (task 06.0)
  FingerprintEnvelope.{h,cpp}, FingerprintProvider.{h,cpp}, FingerprintNormalizer.{h,cpp}
  SuspicionAnalyzer.{h,cpp}, RiskScore.{h,cpp}, FingerprintConfig.{h,cpp}
  providers/      FingerprintProProvider, ThumbmarkProvider, NoopFingerprintProvider

src/tracking/                                # Public tracking endpoints (task 06.0)
  controllers/    FingerprintTrackingController.{h,cpp}
  storage/        FingerprintTrackingRepository.{h,cpp}

tests/
  unit/admin/, integration/admin/, unit/fingerprint/, integration/tracking/,
  fixtures/fingerprints/

docs/admin-console/                          # Task 09.0
  README.md, react-architecture.md, admin-api.md, fingerprinting.md,
  security-and-privacy.md, local-development.md, testing.md, screens.md

.github/workflows/admin-console.yml          # Task 09.0
```

## Global acceptance criteria

- [ ] Anonymous users cannot reach any `/admin/*` page or `/admin/api/v1/*` endpoint.
- [ ] Admin and read-only users can log in and are redirected to `/admin/dashboard`.
- [ ] Read-only users do not see admin-only navigation (Console Users) or admin-only actions.
- [ ] All list endpoints support pagination, sorting, and the shared error format.
- [ ] The dashboard and analytics overview render real data and a correct zero-state on an empty database.
- [ ] URL pairs are paginated, searchable, and support an admin-only, audit-logged disable action.
- [ ] Visitor profiles, fingerprints, and IP addresses are browsable with correct IP/fingerprint masking per role.
- [ ] The `FingerprintProvider` abstraction supports at least the production (Fingerprint Pro) and one local/dev fallback provider without business-logic changes.
- [ ] `SuspicionAnalyzer` produces deterministic risk scores/levels/reasons covering normal, suspicious, and inconsistent fingerprint fixtures.
- [ ] Admins can manage console users (create/disable/reset password/change role); read-only users cannot.
- [ ] Every sensitive action (login, user management, URL pair mutation, export) is visible in the audit log.
- [ ] Aggregate analytics, URL pair, and (policy-gated) raw redirect-event exports work and respect role/permission/masking rules.
- [ ] CI runs frontend lint/typecheck/unit/build/e2e-smoke and backend build/unit/integration jobs.
- [ ] `docs/admin-console/` documents local setup, first-admin creation, fingerprint provider configuration, and privacy/export policy.
