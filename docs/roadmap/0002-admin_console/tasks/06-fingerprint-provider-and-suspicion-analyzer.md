# Task 06 — Fingerprint Provider and Suspicion Analyzer

## Goal

Implement provider abstraction for fingerprint collection and suspicious/unrealistic fingerprint detection.

Production provider: Fingerprint Pro / Fingerprint Identification + Smart Signals.

Fallback provider: ThumbmarkJS or FingerprintJS OSS.

## Required frontend tracking files

If the project has a public web/tracking package, create there. Otherwise create:

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

If existing frontend fingerprinting files exist, extend them instead.

## Required backend files

Create:

```text
src/fingerprint/FingerprintEnvelope.h
src/fingerprint/FingerprintEnvelope.cpp
src/fingerprint/FingerprintProvider.h
src/fingerprint/FingerprintProvider.cpp
src/fingerprint/FingerprintNormalizer.h
src/fingerprint/FingerprintNormalizer.cpp
src/fingerprint/SuspicionAnalyzer.h
src/fingerprint/SuspicionAnalyzer.cpp
src/fingerprint/RiskScore.h
src/fingerprint/RiskScore.cpp
src/fingerprint/FingerprintConfig.h
src/fingerprint/FingerprintConfig.cpp
src/fingerprint/providers/FingerprintProProvider.h
src/fingerprint/providers/FingerprintProProvider.cpp
src/fingerprint/providers/ThumbmarkProvider.h
src/fingerprint/providers/ThumbmarkProvider.cpp
src/fingerprint/providers/NoopFingerprintProvider.h
src/fingerprint/providers/NoopFingerprintProvider.cpp
src/tracking/controllers/FingerprintTrackingController.h
src/tracking/controllers/FingerprintTrackingController.cpp
src/tracking/storage/FingerprintTrackingRepository.h
src/tracking/storage/FingerprintTrackingRepository.cpp
```

## Required public tracking endpoints

```http
POST /api/v1/tracking/fingerprint-event
POST /api/v1/tracking/redirect-beacon
```

## Provider interface

Backend-normalized provider result:

```text
provider
provider_request_id
visitor_id
fingerprint_hash
confidence
collected_at
collection_duration_ms
signals
raw_debug_payload_ref
```

Do not let provider-specific JSON leak into admin API responses.

## SuspicionAnalyzer requirements

Create deterministic rule engine.

Inputs:

```text
normalized fingerprint envelope
request IP record
user-agent
referrer
redirect event
recent visitor history
recent IP history
recent link history
provider smart signals
```

Output:

```text
risk_score
risk_level
risk_reasons[]
```

## Required risk reasons

Implement at least:

```text
bad_bot_detected
anti_detect_browser_detected
browser_tampering_high_confidence
browser_tampering_medium_confidence
high_anomaly_score
virtual_machine_detected
vpn_proxy_tor_or_relay_detected
high_activity_device
many_fingerprints_from_same_ip
many_ips_for_same_fingerprint
fingerprint_regeneration_burst
redirect_rate_too_high
failed_redirect_burst
link_spike_detected
ua_client_hint_inconsistency
timezone_ip_geo_inconsistency
```

## Config

Add config section:

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

## Tests to add

Frontend/public tracker:

```text
web/public-tracker/src/fingerprint/FingerprintClient.test.ts
web/public-tracker/src/redirect/enhancedRedirect.test.ts
web/public-tracker/src/redirect/sendBeacon.test.ts
```

Backend:

```text
tests/unit/fingerprint/FingerprintNormalizerTest.cpp
tests/unit/fingerprint/SuspicionAnalyzerTest.cpp
tests/unit/fingerprint/RiskScoreTest.cpp
tests/unit/fingerprint/FingerprintConfigTest.cpp
tests/integration/tracking/FingerprintTrackingApiTest.cpp
tests/integration/tracking/FingerprintTrackingRateLimitTest.cpp
```

Fixtures:

```text
tests/fixtures/fingerprints/normal_chrome.json
tests/fixtures/fingerprints/bad_bot_selenium.json
tests/fixtures/fingerprints/anti_detect_browser.json
tests/fixtures/fingerprints/high_anomaly_score.json
tests/fixtures/fingerprints/many_ips_same_visitor.json
tests/fixtures/fingerprints/many_fingerprints_same_ip.json
```

## Exit criteria

- Fingerprint provider interface exists.
- Production provider can be configured without changing business logic.
- Fallback provider works in local/dev mode.
- Tracking endpoint validates and stores normalized fingerprint envelope.
- SuspicionAnalyzer produces deterministic risk scores.
- Admin analytics can show risk score and risk reasons.
- Public tracking endpoints reject oversized or stale payloads.
- Unit tests cover normal, suspicious, and inconsistent fingerprints.
