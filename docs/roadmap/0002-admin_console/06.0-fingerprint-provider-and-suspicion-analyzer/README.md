# Task 06.0 - Fingerprint Provider and Suspicion Analyzer

**Parent milestone:** [Milestone 0002 - Admin Console](/docs/roadmap/0002-admin_console/plan.md)
**Status:** ⬜ Not started

## Scope

Implement the provider abstraction for fingerprint collection and the
deterministic suspicious/unrealistic-fingerprint detection engine.
Production provider: Fingerprint Pro / Fingerprint Identification + Smart
Signals. Fallback provider: ThumbmarkJS or FingerprintJS OSS. This task is
what turns the zero/empty risk fields left by tasks 03.0-05.0 into real
data - see
[plan.md - Shared contract C1](/docs/roadmap/0002-admin_console/plan.md)
and
[plan.md - Dependency graph](/docs/roadmap/0002-admin_console/plan.md).

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Backend provider abstraction](01-backend-provider-abstraction.md) | ⬜ Not started | 02, 03 |
| 02 | [Backend suspicion analyzer](02-backend-suspicion-analyzer.md) | ⬜ Not started | 03 |
| 03 | [Backend tracking endpoints](03-backend-tracking-endpoints.md) | ⬜ Not started | 05 |
| 04 | [Frontend public tracker](04-frontend-public-tracker.md) | ⬜ Not started | 05 |
| 05 | [Tests and fixtures](05-tests-and-fixtures.md) | ⬜ Not started | none |

## Key constraints

- Independent of tasks 03.0-05.0's admin UI, but its output (risk_score,
  risk_level, risk_reasons[], normalized signals) is what those tasks'
  dashboard/explorer pages render once populated - land this task promptly
  after the MVP (01.0-04.0) to avoid those pages staying permanently at
  zero-state.
- Provider-specific raw JSON must never leak into the admin API or storage -
  everything is normalized into `ClientFingerprintEnvelope`/
  `FingerprintSignals` first (
  [plan.md - Shared contract C1](/docs/roadmap/0002-admin_console/plan.md)).
- Public tracking endpoints (`/api/v1/tracking/*`) are unauthenticated by
  design but must be rate-limited, size-limited, and must not expose admin
  data - they live outside `/admin/api/v1` on purpose.
- `SuspicionAnalyzer` must be deterministic and configurable (risk
  thresholds in config, not hardcoded), so tests are reproducible.
