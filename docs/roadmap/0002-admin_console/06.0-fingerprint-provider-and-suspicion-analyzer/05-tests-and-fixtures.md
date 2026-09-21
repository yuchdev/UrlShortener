# 05 - Tests and Fixtures

**Parent task:** 06.0 Fingerprint Provider and Suspicion Analyzer
**State:** ⬜ Not started
**Depends on:** 03, 04
**Blocks:** none

## Objective

Cover the normalizer, suspicion analyzer, risk scorer, config, tracking
endpoints, and public tracker with unit/integration tests, backed by
realistic synthetic fingerprint fixtures.

## Files to add

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

## Requirements

1. Fixtures are synthetic, inspired by CreepJS-style inconsistent/headless/
   stealth fingerprint patterns, but must not depend on CreepJS as a runtime
   library anywhere (
   [README.md source](/docs/roadmap/0002-admin_console/00-decision-record.md) -
   testing-only reference, never a production dependency).
2. `FingerprintNormalizerTest` covers each provider's raw shape normalizing
   correctly into `ClientFingerprintEnvelope`.
3. `SuspicionAnalyzerTest` runs every fixture above and asserts the expected
   `risk_reasons[]` fire (e.g. `bad_bot_selenium.json` ->
   `bad_bot_detected`; `many_ips_same_visitor.json` ->
   `many_ips_for_same_fingerprint`-family reason).
4. `RiskScoreTest` covers score-to-level boundary mapping and config-driven
   threshold overrides.
5. `FingerprintConfigTest` covers YAML parsing and defaults.
6. `FingerprintTrackingApiTest` covers valid submission end-to-end (through
   normalization, scoring, and persistence) and validation-failure paths.
7. `FingerprintTrackingRateLimitTest` covers per-IP and per-event-id rate
   limiting.
8. Public-tracker tests cover client-side collection, timeout-bound
   redirect, and beacon fallback behavior (mirroring subtask 04's success
   criteria).

## Constraints

- `normal_chrome.json` must exist and produce a `low` risk level - a
  suite that only tests suspicious fixtures cannot prove the analyzer
  doesn't over-trigger on legitimate traffic.

## Success criteria

- [ ] All fixtures produce their documented expected risk reason(s) and
      risk level.
- [ ] `normal_chrome.json` produces `low` risk with an empty or minimal
      `risk_reasons[]`.
- [ ] All backend unit/integration tests above pass under CTest.
- [ ] All public-tracker unit tests above pass under its test runner.
- [ ] Unit tests cover normal, suspicious, and inconsistent fingerprints, as
      required by the source task's exit criteria.
