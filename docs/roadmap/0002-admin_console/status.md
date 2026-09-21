# Milestone 0002 - Admin Console - Status

Tracks progress against [plan.md](/docs/roadmap/0002-admin_console/plan.md).

## Current status

| Task | Name | Status | Tests |
|------|------|--------|-------|
| 01.0 | React Admin Shell and Safe Login Integration | ⬜ Not started | 0 |
| 02.0 | Admin API Query Foundation | ⬜ Not started | 0 |
| 03.0 | Analytics Dashboard | ⬜ Not started | 0 |
| 04.0 | URL Pair List and URL Pair Analytics | ⬜ Not started | 0 |
| 05.0 | Visitor, Fingerprint, and IP Explorer | ⬜ Not started | 0 |
| 06.0 | Fingerprint Provider and Suspicion Analyzer | ⬜ Not started | 0 |
| 07.0 | Console Users and Audit Log | ⬜ Not started | 0 |
| 08.0 | Exports, Privacy, and Permissions | ⬜ Not started | 0 |
| 09.0 | Tests, CI, and Documentation | ⬜ Not started | 0 |

**Legend:** ✅ Complete · 🔶 In progress / partial · ⬜ Not started

**Current gate status:** 0% implemented. Verified: no `web/admin/`, no
`src/admin/`, no `package.json` anywhere in the repository as of this
milestone's restructuring (2026-09-21). This milestone is design-complete
(7 supporting spec docs + 9 tasks decomposed into 37 subtasks) but has no
code yet. Scope is the MVP milestone first: React admin shell + safe login +
analytics overview + URL pair list + URL pair detail analytics (tasks
01.0-04.0), then identity exploration, fingerprinting, admin ops, privacy,
and test/CI/docs hardening (tasks 05.0-09.0). Key stack decisions from the
design phase remain valid and unchanged: React + TypeScript + Vite +
TanStack Router/Query/Table + Apache ECharts + Tailwind CSS, with
Fingerprint Pro / Fingerprint Identification + Smart Signals as the
production fingerprint provider and ThumbmarkJS-or-FingerprintJS-OSS as the
local/dev fallback behind a shared `FingerprintProvider` interface.

## Notes & decisions

- **Why Fingerprint Pro.** The requirement is not just a stable `visitorId`
  but detection of suspicious/unrealistic fingerprints (bot, tamper,
  anti-detect-browser, VPN/proxy/Tor, anomaly scoring). Browser-only
  libraries compute their result client-side and are easier to spoof, so
  Fingerprint Pro (server-side signals) is the production choice, with
  ThumbmarkJS or FingerprintJS OSS as an explicitly weaker local/dev fallback
  behind the same provider interface. An internal `SuspicionAnalyzer` always
  re-scores every fingerprint regardless of provider. Full rationale:
  [00-decision-record.md](/docs/roadmap/0002-admin_console/00-decision-record.md).
- **MVP non-goals (first milestone):** public self-registration, full visual
  design polish, complex RBAC editor UI, real-time streaming dashboards,
  deleting analytics data from the UI, exposing raw fingerprint components to
  normal users. See [plan.md - Shared contracts C8](/docs/roadmap/0002-admin_console/plan.md).
- **Document map (supporting specs, kept at milestone root, untouched by this
  restructuring):**
  - [00-decision-record.md](/docs/roadmap/0002-admin_console/00-decision-record.md) - stack + fingerprinting + auth + naming decisions
  - [01-product-and-ux-spec.md](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md) - navigation, pages, filters, empty/error states
  - [02-react-frontend-architecture.md](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md) - directory layout, routes, API client, React Query, tables, charts
  - [03-fingerprint-and-suspicion-model.md](/docs/roadmap/0002-admin_console/03-fingerprint-and-suspicion-model.md) - provider strategy, envelope schema, suspicion rules, risk scoring
  - [04-admin-api-contract.md](/docs/roadmap/0002-admin_console/04-admin-api-contract.md) - full endpoint list and response/error shapes
  - [05-data-model-and-storage.md](/docs/roadmap/0002-admin_console/05-data-model-and-storage.md) - redirect event, fingerprint, IP, visitor profile schemas, indexes, retention
  - [06-security-privacy-permissions.md](/docs/roadmap/0002-admin_console/06-security-privacy-permissions.md) - roles, permissions, IP/fingerprint masking, audit log, CSRF

## Decomposition tree (as built)

```text
docs/roadmap/0002-admin_console/
  plan.md
  status.md
  00-decision-record.md
  01-product-and-ux-spec.md
  02-react-frontend-architecture.md
  03-fingerprint-and-suspicion-model.md
  04-admin-api-contract.md
  05-data-model-and-storage.md
  06-security-privacy-permissions.md
  01.0-react-admin-shell-safe-login/          (4 subtasks)
    README.md
    01-frontend-app-shell.md
    02-backend-session-auth.md
    03-frontend-auth-integration.md
    04-tests-and-e2e.md
  02.0-admin-api-query-foundation/             (4 subtasks)
    README.md
    01-backend-query-primitives.md
    02-frontend-query-models-and-hooks.md
    03-frontend-table-and-filter-components.md
    04-tests.md
  03.0-analytics-dashboard/                    (3 subtasks)
    README.md
    01-backend-analytics-overview.md
    02-frontend-dashboard-ui.md
    03-tests.md
  04.0-url-pair-analytics/                     (4 subtasks)
    README.md
    01-backend-url-pairs-crud.md
    02-backend-link-analytics.md
    03-frontend-url-pairs-and-link-analytics-ui.md
    04-tests.md
  05.0-visitor-fingerprint-ip-explorer/        (5 subtasks)
    README.md
    01-backend-visitor-profiles-api.md
    02-backend-fingerprints-and-ips-api.md
    03-frontend-privacy-primitives.md
    04-frontend-explorer-pages.md
    05-tests.md
  06.0-fingerprint-provider-and-suspicion-analyzer/  (5 subtasks)
    README.md
    01-backend-provider-abstraction.md
    02-backend-suspicion-analyzer.md
    03-backend-tracking-endpoints.md
    04-frontend-public-tracker.md
    05-tests-and-fixtures.md
  07.0-console-users-and-audit-log/            (5 subtasks)
    README.md
    01-backend-console-users.md
    02-backend-audit-log.md
    03-frontend-console-users-ui.md
    04-frontend-audit-log-ui.md
    05-tests.md
  08.0-exports-privacy-and-permissions/        (4 subtasks)
    README.md
    01-backend-privacy-and-masking.md
    02-backend-export-service.md
    03-frontend-export-and-privacy-ui.md
    04-tests.md
  09.0-tests-ci-and-documentation/              (3 subtasks)
    README.md
    01-frontend-test-infrastructure.md
    02-backend-test-groups-and-ci.md
    03-documentation.md
```

9 task folders, 37 subtask files total.

## Per-task detail

Nothing is implemented yet. Each task folder's `README.md` carries its own
`⬜ Not started` status and subtask table; see the individual subtask files
for concrete file lists, endpoint lists, and per-subtask success criteria.
This section will gain per-task narrative detail once a task moves past
`⬜ Not started`.
