# 02 — React Frontend Architecture

## 1. Directory layout

Create the React app under:

```text
web/admin/
```

Recommended structure:

```text
web/admin/
  package.json
  tsconfig.json
  vite.config.ts
  index.html
  src/
    main.tsx
    app/
      App.tsx
      router.tsx
      routes.tsx
      providers.tsx
      config.ts
    auth/
      AuthProvider.tsx
      ProtectedRoute.tsx
      PermissionGate.tsx
      authTypes.ts
      useAuth.ts
    api/
      apiClient.ts
      authApi.ts
      analyticsApi.ts
      urlPairsApi.ts
      visitorProfilesApi.ts
      fingerprintsApi.ts
      ipsApi.ts
      consoleUsersApi.ts
      auditLogApi.ts
      exportsApi.ts
      schemas.ts
    components/
      layout/
        AdminLayout.tsx
        Sidebar.tsx
        TopBar.tsx
        Breadcrumbs.tsx
      ui/
        Button.tsx
        Card.tsx
        Dialog.tsx
        Input.tsx
        Select.tsx
        Badge.tsx
        Spinner.tsx
        EmptyState.tsx
        ErrorState.tsx
      analytics/
        MetricCard.tsx
        TimeSeriesChart.tsx
        TopListCard.tsx
        RiskBadge.tsx
        StatusBadge.tsx
        DateRangePicker.tsx
        GlobalFilterBar.tsx
      tables/
        PaginatedTable.tsx
        TableToolbar.tsx
        ColumnVisibilityMenu.tsx
      privacy/
        MaskedIp.tsx
        FingerprintId.tsx
        SensitiveValue.tsx
    pages/
      LoginPage.tsx
      DashboardPage.tsx
      AnalyticsOverviewPage.tsx
      LinkAnalyticsPage.tsx
      LinkDetailPage.tsx
      UrlPairsPage.tsx
      VisitorProfilesPage.tsx
      VisitorProfileDetailPage.tsx
      FingerprintsPage.tsx
      FingerprintDetailPage.tsx
      IpAddressesPage.tsx
      IpAddressDetailPage.tsx
      ReferrersPage.tsx
      SecurityAnalyticsPage.tsx
      ConsoleUsersPage.tsx
      AuditLogPage.tsx
      SettingsPage.tsx
    model/
      auth.ts
      analytics.ts
      urlPair.ts
      visitorProfile.ts
      fingerprint.ts
      ipAddress.ts
      auditLog.ts
      pagination.ts
      permissions.ts
    hooks/
      useDateRange.ts
      usePagination.ts
      useSearchParamsState.ts
      useDebouncedValue.ts
    test/
      msw/
        handlers.ts
        server.ts
      fixtures/
        analyticsFixtures.ts
        authFixtures.ts
```

## 2. Route structure

```text
/admin/login
/admin/dashboard
/admin/analytics/overview
/admin/analytics/links
/admin/analytics/links/$urlPairId
/admin/analytics/visitors
/admin/analytics/fingerprints
/admin/analytics/ips
/admin/analytics/referrers
/admin/analytics/security
/admin/url-pairs
/admin/visitor-profiles
/admin/visitor-profiles/$visitorProfileId
/admin/fingerprints
/admin/fingerprints/$fingerprintId
/admin/ips
/admin/ips/$ipId
/admin/console-users
/admin/audit-log
/admin/settings
```

## 3. API client rules

The frontend API client must:

- send requests only to `/admin/api/v1`;
- include credentials for session cookies;
- attach CSRF token for mutating requests;
- parse API errors into typed frontend errors;
- validate key responses with Zod in development/test mode;
- never expose credential hashes or raw auth records.

Example API client behavior:

```ts
fetch(url, {
  credentials: 'include',
  headers: {
    'Content-Type': 'application/json',
    'X-CSRF-Token': csrfToken,
  },
})
```

## 4. Auth model

`AuthProvider` must load `/auth/me` on startup.

Auth states:

```text
checking
authenticated
anonymous
error
```

`ProtectedRoute` behavior:

```text
checking -> show app loader
anonymous -> redirect to /admin/login
authenticated -> render route
```

`PermissionGate` hides frontend actions but is not security enforcement.

Backend permissions are authoritative.

## 5. React Query conventions

Query keys must be stable and hierarchical.

Examples:

```ts
['auth', 'me']
['analytics', 'overview', filters]
['analytics', 'links', filters, pagination]
['urlPairs', filters, pagination]
['visitorProfiles', filters, pagination]
['fingerprints', filters, pagination]
['ips', filters, pagination]
['auditLog', filters, pagination]
```

Use short stale times for dashboard queries and longer stale times for static config.

Recommended defaults:

```text
dashboard analytics: staleTime 30 seconds
list pages: staleTime 15 seconds
auth/me: staleTime 60 seconds
settings/config: staleTime 5 minutes
```

## 6. Tables

All tables must use the same `PaginatedTable` component.

Required features:

```text
server-side pagination
server-side sorting
server-side filtering
loading state
empty state
error state
column visibility
row click action
compact density
copy ID action
```

Pagination DTO:

```ts
export interface PageRequest {
  page: number;
  page_size: number;
  sort_by?: string;
  sort_direction?: 'asc' | 'desc';
}

export interface PageResponse<T> {
  items: T[];
  pagination: {
    page: number;
    page_size: number;
    total_items: number;
    total_pages: number;
  };
}
```

## 7. Charts

Do not use chart-library objects directly in pages.

Create wrappers:

```text
TimeSeriesChart
StackedBarChart
TopNBarChart
DonutChart
HeatmapChart
```

Initial MVP requires only:

```text
TimeSeriesChart
TopNBarChart
```

## 8. Privacy components

Create reusable masking components:

```text
MaskedIp
SensitiveValue
FingerprintId
VisitorProfileLink
```

Rules:

```text
admin may see full IP when backend allows it
readonly sees masked IP by default
raw fingerprint components are hidden by default for all users
```

The frontend must display only what the backend returns. It must not reconstruct masked data.

## 9. Build output

The React app must be buildable as static assets:

```bash
cd web/admin
npm ci
npm run build
```

Output:

```text
web/admin/dist/
```

The backend may serve this directory under `/admin`.

## 10. Testing approach

Frontend tests:

```text
Vitest unit tests for hooks and pure components
React Testing Library for pages
MSW for API mocks
Playwright for login and navigation flows
```

Required initial tests:

```text
Login page validates input
Protected routes redirect anonymous users
Dashboard renders metrics from mocked API
Read-only user does not see admin-only actions
URL pairs table handles pagination
API error state is shown
```
