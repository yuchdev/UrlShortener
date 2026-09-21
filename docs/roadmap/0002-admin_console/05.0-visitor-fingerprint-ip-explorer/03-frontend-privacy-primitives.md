# 03 - Frontend Privacy Primitives

**Parent task:** 05.0 Visitor, Fingerprint, and IP Explorer
**State:** ⬜ Not started
**Depends on:** none (parallel with subtasks 01-02; consumed by subtask 04)
**Blocks:** 04

## Objective

Build the reusable masking/display components and identity-detail widgets
(signals panel, risk reasons, timeline) that every explorer detail page
(subtask 04) and later fingerprint-related UI (task 06.0/08.0) composes
from.

## Files to add

```text
web/admin/src/components/privacy/MaskedIp.tsx
web/admin/src/components/privacy/FingerprintId.tsx
web/admin/src/components/privacy/SensitiveValue.tsx
web/admin/src/components/analytics/FingerprintSignalsPanel.tsx
web/admin/src/components/analytics/RiskReasonsList.tsx
web/admin/src/components/analytics/IdentityTimeline.tsx
```

## Requirements

1. `MaskedIp`, `SensitiveValue` render exactly what the backend returns -
   they must not attempt to mask/unmask client-side (
   [02-react-frontend-architecture.md §8](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md),
   [plan.md - Shared contract C5](/docs/roadmap/0002-admin_console/plan.md)).
2. `FingerprintId` renders a fingerprint's internal ID (never raw
   components) with a copy-to-clipboard affordance, reusing the
   `PaginatedTable` "copy ID action" convention from task 02.0.
3. `FingerprintSignalsPanel` renders the normalized `FingerprintSignals`
   fields (bot, incognito, vpn, proxy, tor, relay, datacenter, tampering,
   tampering_confidence, anomaly_score, anti_detect_browser,
   virtual_machine, developer_tools, ...) from
   [03-fingerprint-and-suspicion-model.md §6](/docs/roadmap/0002-admin_console/03-fingerprint-and-suspicion-model.md) -
   it must degrade gracefully (e.g. "no signals recorded") when fields are
   absent, since task 06.0 is what actually populates them.
4. `RiskReasonsList` renders a `risk_reasons[]` array with badges/labels;
   must handle an empty array (pre-06.0 state) as a normal, non-error case.
5. `IdentityTimeline` renders a mixed chronological feed (fingerprint
   changes, IP changes, redirect events) for visitor/fingerprint/IP detail
   pages.

## Constraints

- These components must not fetch data themselves - they are presentational,
  receiving typed props from the pages built in subtask 04.

## Success criteria

- [ ] `MaskedIp` renders a masked value, a full value, and an absent value
      correctly (three distinct render paths, no client-side guessing).
- [ ] `FingerprintSignalsPanel` renders a fully-populated signals object and
      an empty/absent one without error.
- [ ] `RiskReasonsList` renders zero, one, and many reasons correctly.
- [ ] `IdentityTimeline` renders a mixed-event-type fixture in chronological
      order.
