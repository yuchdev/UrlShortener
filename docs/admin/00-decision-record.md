# 00 — Decision Record

## 1. React frontend stack

Use React as the main admin-console technology.

Recommended stack:

```text
React
TypeScript
Vite
TanStack Router
TanStack Query
TanStack Table
TanStack Virtual
Apache ECharts
React Hook Form
Zod
Tailwind CSS
shadcn/ui-style internal components
Vitest
React Testing Library
Playwright
MSW
```

## 2. Rationale

### React + TypeScript

React is a safe long-term choice for admin consoles, analytics dashboards, and table-heavy interfaces. TypeScript is mandatory because the console will contain many DTOs, filters, role checks, chart models, and generated API types.

### Vite

Use Vite for fast local development and simple production bundling.

### TanStack Query

Use TanStack Query for:

- API request caching;
- stale/retry behavior;
- automatic refetching;
- query invalidation after admin actions;
- clean loading/error states.

### TanStack Router

Use TanStack Router instead of ad-hoc routing because the app needs typed nested routes, route-level loaders, search params, and protected route branches.

### TanStack Table + Virtual

Use TanStack Table for all paginated tables and TanStack Virtual for potentially large rows.

The admin console must handle:

- URL pairs;
- redirect events;
- fingerprints;
- IP addresses;
- visitor profiles;
- audit log entries.

A generic table layer is required.

### Apache ECharts

Use Apache ECharts for analytics charts because the app will likely need line charts, stacked bars, heatmaps, top lists, and eventually larger interactive datasets.

Wrap it internally so the library can be replaced later.

Do not spread chart-library-specific code across pages.

### Tailwind + internal component wrappers

Use Tailwind for layout and utility classes. Build internal components instead of depending directly on a large design system everywhere.

Suggested internal components:

```text
Button
Card
Dialog
Dropdown
Input
Select
TableShell
MetricCard
RiskBadge
StatusBadge
DateRangePicker
PermissionGate
MaskedValue
```

## 3. Fingerprinting decision

### Production provider

Use **Fingerprint Pro / Fingerprint Identification + Smart Signals** for production-grade visitor identification and suspicious fingerprint detection.

The important requirement is not just a stable `visitorId`. The important requirement is detection of suspicious, unrealistic, or manipulated fingerprints.

The selected provider must support at least:

```text
visitor_id
request_id
confidence
bot signal
incognito/private signal
VPN/proxy/relay indicators
tampering signal
tampering confidence
tampering ML score
anomaly score
anti-detect-browser flag
virtual machine flag
developer-tools/automation indicators
velocity signals
IP/visitor relationship signals
```

### Local/dev provider

Use a provider interface with a fallback implementation:

```text
FingerprintProvider
  FingerprintProProvider
  ThumbmarkProvider
  FingerprintJsOssProvider
  NoopFingerprintProvider
```

The backend must not depend on one provider's raw JSON shape.

Normalize all results into the internal DTO:

```text
ClientFingerprintEnvelope
```

## 4. Safe login decision

The admin console must use the safe login system implemented in the previous task.

Rules:

- no browser-side credential storage;
- no direct browser access to credential database;
- server-side session;
- secure `HttpOnly` cookie;
- CSRF protection for mutating requests;
- role-based authorization in backend;
- frontend hides unavailable actions but does not enforce security by itself.

## 5. Naming decision

Use these terms consistently:

| Term | Meaning |
|---|---|
| Console user | Person who logs into the admin console |
| Visitor profile | External visitor identity derived from fingerprint/IP history |
| Fingerprint | Browser/device identity signal |
| IP record | Observed network address identity |
| URL pair | Short code + target URL |
| Redirect event | Immutable analytics event produced by a short-link request |

Do not call visitor profiles simply “users” in the UI.
