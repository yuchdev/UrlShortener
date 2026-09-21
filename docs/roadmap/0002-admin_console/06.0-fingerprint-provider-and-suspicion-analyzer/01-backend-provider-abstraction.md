# 01 - Backend Provider Abstraction

**Parent task:** 06.0 Fingerprint Provider and Suspicion Analyzer
**State:** ⬜ Not started
**Depends on:** none
**Blocks:** 02, 03

## Objective

Implement the `FingerprintProvider` interface, its normalization layer, and
the production/fallback/noop provider implementations, plus the
fingerprinting config section.

## Files to add

```text
src/fingerprint/FingerprintEnvelope.h
src/fingerprint/FingerprintEnvelope.cpp
src/fingerprint/FingerprintProvider.h
src/fingerprint/FingerprintProvider.cpp
src/fingerprint/FingerprintNormalizer.h
src/fingerprint/FingerprintNormalizer.cpp
src/fingerprint/FingerprintConfig.h
src/fingerprint/FingerprintConfig.cpp
src/fingerprint/providers/FingerprintProProvider.h
src/fingerprint/providers/FingerprintProProvider.cpp
src/fingerprint/providers/ThumbmarkProvider.h
src/fingerprint/providers/ThumbmarkProvider.cpp
src/fingerprint/providers/NoopFingerprintProvider.h
src/fingerprint/providers/NoopFingerprintProvider.cpp
```

## Provider interface

```text
FingerprintProvider
  FingerprintProProvider       production
  ThumbmarkProvider            local/dev fallback
  NoopFingerprintProvider      tests/minimal mode
```

(`FingerprintJsOssProvider` is an equivalent secondary fallback; implement
it only if `ThumbmarkProvider` alone does not satisfy local/dev needs - the
interface must support adding it later without changes elsewhere.)

Normalized provider result (
[03-fingerprint-and-suspicion-model.md §6](/docs/roadmap/0002-admin_console/03-fingerprint-and-suspicion-model.md)):

```text
provider, provider_request_id, visitor_id, fingerprint_hash, confidence,
collected_at, collection_duration_ms, signals, raw_debug_payload_ref
```

## Config

Add to the app config schema:

```yaml
fingerprinting:
  mode: fast_redirect | enhanced_analytics | disabled
  provider: fingerprint_pro | thumbmark | fingerprintjs_oss | noop
  enhanced_redirect_timeout_ms: 250
  store_raw_debug_payloads: false
  raw_debug_payload_retention_hours: 24
  risk_thresholds:
    medium: 25
    high: 50
    critical: 75
```

## Requirements

1. `FingerprintProvider` is an abstract interface; each concrete provider
   implements collection/lookup and returns a raw, provider-specific result.
2. `FingerprintNormalizer` converts any provider's raw result into
   `ClientFingerprintEnvelope`/`FingerprintSignals` - this is the one place
   provider-specific parsing is allowed to exist.
3. `FingerprintProProvider` must support at minimum: `visitor_id`,
   `request_id`, `confidence`, bot signal, incognito/private signal, VPN/
   proxy/relay indicators, tampering signal + confidence + ML score,
   anomaly score, anti-detect-browser flag, virtual machine flag, developer-
   tools/automation indicators, velocity signals (
   [03-fingerprint-and-suspicion-model.md §3](/docs/roadmap/0002-admin_console/03-fingerprint-and-suspicion-model.md)).
4. `ThumbmarkProvider` (fallback) must document its explicit limitations in
   code comments: browser-only fingerprint is easier to spoof, server cannot
   fully validate browser-computed values, and its `confidence` must be
   lower than the production provider's (
   [03-fingerprint-and-suspicion-model.md §4](/docs/roadmap/0002-admin_console/03-fingerprint-and-suspicion-model.md)).
5. `FingerprintConfig` parses the `fingerprinting:` YAML section and is the
   single source of risk thresholds consumed by task 06.0 subtask 02.
6. Provider selection is config-driven (`provider:` field) with no business-
   logic changes required to switch providers.

## Constraints

- Provider raw JSON must never escape `FingerprintNormalizer` - no other
  file in the codebase should parse a provider-specific payload shape.
- `store_raw_debug_payloads` defaults to `false`; when enabled, raw payloads
  must be stored separately, restricted to admin-only access, and expire
  per `raw_debug_payload_retention_hours` (
  [03-fingerprint-and-suspicion-model.md §9](/docs/roadmap/0002-admin_console/03-fingerprint-and-suspicion-model.md)) -
  the retention/access mechanics may be a follow-up if storage wiring
  belongs elsewhere, but the config flag and normalizer hook must exist now.

## Success criteria

- [ ] `FingerprintProvider` interface exists with at least
      `FingerprintProProvider`, `ThumbmarkProvider`, and
      `NoopFingerprintProvider` implementations.
- [ ] Switching `fingerprinting.provider` in config selects a different
      provider without touching call sites.
- [ ] `FingerprintNormalizer` produces a valid `ClientFingerprintEnvelope`
      from each provider's raw fixture output.
- [ ] `FingerprintConfig` parses all documented YAML fields with sane
      defaults.
