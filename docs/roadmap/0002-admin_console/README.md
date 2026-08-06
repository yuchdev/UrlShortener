# URL Shortener Web Admin Console — Final React Specification

## Purpose

Build a React-based web admin console for the URL shortener project.

The console is **analytics-first**. Tables for URL pairs, fingerprints, IP addresses, and visitor profiles are required, but they are secondary to traffic analytics, abuse detection, identity/fingerprint exploration, and operational visibility.

## Main decisions

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

## Why Fingerprint Pro is selected

The requirement is not only stable browser fingerprinting. It explicitly requires detection of **suspicious or unrealistic fingerprints**.

Browser-only libraries are useful for local analytics, but they are weaker against spoofing because their result is computed in the browser. The production provider must therefore support server-side analysis, bot/tamper signals, velocity signals, anomaly scoring, VPN/proxy indicators, and anti-detect-browser detection.

Use this hierarchy:

1. **Production:** Fingerprint Pro / Fingerprint Identification + Smart Signals.
2. **Local/dev/offline:** ThumbmarkJS provider or FingerprintJS OSS provider.
3. **Internal scoring:** always run our own SuspicionAnalyzer over provider signals and local redirect history.
4. **Testing:** include synthetic fixtures inspired by CreepJS-style inconsistent/headless/stealth fingerprints, but do not depend on CreepJS as a production runtime library.

## Document map

```text
urlshortener_admin_console_spec/
  README.md
  00-decision-record.md
  01-product-and-ux-spec.md
  02-react-frontend-architecture.md
  03-fingerprint-and-suspicion-model.md
  04-admin-api-contract.md
  05-data-model-and-storage.md
  06-security-privacy-permissions.md
  tasks/
    01-react-admin-shell-safe-login.md
    02-admin-api-query-foundation.md
    03-analytics-dashboard.md
    04-url-pair-analytics.md
    05-visitor-fingerprint-ip-explorer.md
    06-fingerprint-provider-and-suspicion-analyzer.md
    07-console-users-and-audit-log.md
    08-exports-privacy-and-permissions.md
    09-tests-ci-and-documentation.md
```

## MVP milestone

The first useful milestone is:

```text
React admin shell + safe login + analytics overview + URL pair list + URL pair detail analytics
```

This gives an immediately useful console while keeping implementation scope controlled.

## Non-goals for the first milestone

- public self-registration;
- full visual design polish;
- complex RBAC editor UI;
- real-time streaming dashboards;
- deleting analytics data from UI;
- exposing raw fingerprint components to normal users.
