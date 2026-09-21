# 03 - Backend Tracking Endpoints

**Parent task:** 06.0 Fingerprint Provider and Suspicion Analyzer
**State:** ⬜ Not started
**Depends on:** 01, 02
**Blocks:** 05

## Objective

Implement the public, unauthenticated tracking endpoints that receive
fingerprint envelopes and redirect beacons from the frontend tracker
(subtask 04), running them through the normalizer and `SuspicionAnalyzer`
and persisting the result.

## Files to add

```text
src/tracking/controllers/FingerprintTrackingController.h
src/tracking/controllers/FingerprintTrackingController.cpp
src/tracking/storage/FingerprintTrackingRepository.h
src/tracking/storage/FingerprintTrackingRepository.cpp
```

## API contract

```http
POST /api/v1/tracking/fingerprint-event
POST /api/v1/tracking/redirect-beacon
```

These are **not** admin endpoints - they live outside `/admin/api/v1`, do
not require admin authentication, and must not expose admin data (
[04-admin-api-contract.md §9](/docs/roadmap/0002-admin_console/04-admin-api-contract.md)).

## Requirements

1. Validate every inbound request:
   `short_code` or `redirect_event_id`, `provider`, `visitor_id`/
   `fingerprint_hash` format, payload size, collection timestamp freshness (
   [04-admin-api-contract.md §9](/docs/roadmap/0002-admin_console/04-admin-api-contract.md)).
2. Rate-limit by IP and event id; reject oversized payloads; reject stale
   timestamps; never reflect raw input back in error responses (
   [06-security-privacy-permissions.md §10](/docs/roadmap/0002-admin_console/06-security-privacy-permissions.md)).
3. On a valid `fingerprint-event`, run the payload through
   `FingerprintNormalizer` then `SuspicionAnalyzer` (subtasks 01-02), and
   persist via `FingerprintTrackingRepository` into the `fingerprint` /
   `fingerprint_signal` / `fingerprint_regeneration_event` tables (
   [05-data-model-and-storage.md §3-4, §7](/docs/roadmap/0002-admin_console/05-data-model-and-storage.md)) -
   this is what finally makes tasks 03.0/04.0/05.0's risk fields non-zero.
4. `redirect-beacon` supports late analytics data via
   `navigator.sendBeacon()`/`fetch(keepalive)` semantics on the client side
   (subtask 04) - the backend must accept a beacon arriving after the
   original redirect completed and must not treat beacon failure/absence as
   an error condition anywhere else in the system (
   [03-fingerprint-and-suspicion-model.md §5.3](/docs/roadmap/0002-admin_console/03-fingerprint-and-suspicion-model.md)).
5. Respect `fingerprinting.mode` from config: in `fast_redirect` mode,
   fingerprint enrichment may legitimately be absent; in `enhanced_analytics`
   mode, the redirect endpoint (owned by the core redirect path, not this
   task) is expected to have already given the client
   `enhanced_redirect_timeout_ms` to call this endpoint before redirecting;
   in `disabled` mode, this endpoint should reject or no-op cleanly rather
   than error.

## Constraints

- If fingerprint collection fails or times out, the redirect itself must
  still happen (enforced in the redirect path, not here) unless the link is
  explicitly configured as protected - this endpoint must not become a
  blocking dependency for the redirect flow.

## Success criteria

- [ ] Valid `fingerprint-event` payloads are normalized, risk-scored, and
      persisted.
- [ ] Oversized, stale, or malformed payloads are rejected without
      reflecting raw input in the error.
- [ ] Both endpoints are rate-limited by IP and event id.
- [ ] Neither endpoint requires or exposes admin authentication/data.
- [ ] `redirect-beacon` accepts late-arriving beacon data without erroring.
