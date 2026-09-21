# 04 - Frontend Public Tracker

**Parent task:** 06.0 Fingerprint Provider and Suspicion Analyzer
**State:** ⬜ Not started
**Depends on:** none (integrates against subtask 03's endpoint contract,
which is fixed by
[04-admin-api-contract.md §9](/docs/roadmap/0002-admin_console/04-admin-api-contract.md))
**Blocks:** 05

## Objective

Build the public, unauthenticated tracker bundle that runs in the visitor's
browser during the redirect flow: fingerprint collection, provider clients,
and the enhanced-redirect/beacon glue.

## Files to add

If the project already has frontend fingerprinting files, extend them
instead of creating a parallel package. Otherwise create:

```text
web/public-tracker/package.json
web/public-tracker/tsconfig.json
web/public-tracker/vite.config.ts
web/public-tracker/src/fingerprint/FingerprintEnvelope.ts
web/public-tracker/src/fingerprint/FingerprintClient.ts
web/public-tracker/src/fingerprint/FingerprintProvider.ts
web/public-tracker/src/fingerprint/providers/FingerprintProClient.ts
web/public-tracker/src/fingerprint/providers/ThumbmarkClient.ts
web/public-tracker/src/fingerprint/providers/FingerprintJsOssClient.ts
web/public-tracker/src/fingerprint/providers/NoopFingerprintClient.ts
web/public-tracker/src/redirect/enhancedRedirect.ts
web/public-tracker/src/redirect/sendBeacon.ts
```

## Requirements

1. `FingerprintEnvelope.ts` mirrors the backend `ClientFingerprintEnvelope`/
   `FingerprintSignals` TypeScript shapes exactly (
   [03-fingerprint-and-suspicion-model.md §6](/docs/roadmap/0002-admin_console/03-fingerprint-and-suspicion-model.md)).
2. `FingerprintProvider.ts` defines a client-side provider interface with
   implementations `FingerprintProClient`, `ThumbmarkClient`,
   `FingerprintJsOssClient`, `NoopFingerprintClient` - mirroring the backend
   provider abstraction (subtask 01) so the same mental model applies on
   both sides.
3. `FingerprintClient.ts` orchestrates: pick provider from config, collect,
   normalize into the envelope shape, POST to
   `/api/v1/tracking/fingerprint-event`.
4. `enhancedRedirect.ts` implements the enhanced-analytics collection mode (
   [03-fingerprint-and-suspicion-model.md §5.2](/docs/roadmap/0002-admin_console/03-fingerprint-and-suspicion-model.md)):
   starts fingerprint collection, sends the envelope, then redirects to the
   target URL after collection completes or a strict timeout elapses
   (default 150-300ms, configurable, matching
   `fingerprinting.enhanced_redirect_timeout_ms`) - redirect must still
   happen if collection fails, unless the link is explicitly protected.
5. `sendBeacon.ts` implements the beacon mode using
   `navigator.sendBeacon()` with a `fetch(..., {keepalive:true})` fallback,
   for `POST /api/v1/tracking/redirect-beacon`; beacon success/failure must
   never block or affect redirect correctness (
   [03-fingerprint-and-suspicion-model.md §5.3](/docs/roadmap/0002-admin_console/03-fingerprint-and-suspicion-model.md)).

## Constraints

- This bundle is served to anonymous visitors - it must not import anything
  from `web/admin/` (no auth state, no admin API client, no session
  awareness).
- Keep the timeout enforcement client-side authoritative for UX, but the
  backend (subtask 03) must not depend on the client honoring it.

## Success criteria

- [ ] `FingerprintClient` produces a valid envelope for each provider
      client, normalized to the shared TypeScript shape.
- [ ] `enhancedRedirect` redirects within the configured timeout even when
      collection is artificially delayed past it.
- [ ] `enhancedRedirect` still redirects when collection throws/fails.
- [ ] `sendBeacon` falls back to `fetch(keepalive)` when
      `navigator.sendBeacon` is unavailable.
