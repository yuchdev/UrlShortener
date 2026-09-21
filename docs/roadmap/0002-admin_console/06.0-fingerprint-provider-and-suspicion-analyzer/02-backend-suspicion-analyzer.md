# 02 - Backend Suspicion Analyzer

**Parent task:** 06.0 Fingerprint Provider and Suspicion Analyzer
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** 03

## Objective

Implement the deterministic `SuspicionAnalyzer` rule engine and `RiskScore`
calculator that turns a normalized fingerprint envelope plus request/history
context into `risk_score`, `risk_level`, and `risk_reasons[]`.

## Files to add

```text
src/fingerprint/SuspicionAnalyzer.h
src/fingerprint/SuspicionAnalyzer.cpp
src/fingerprint/RiskScore.h
src/fingerprint/RiskScore.cpp
```

## Inputs / outputs

Inputs (
[03-fingerprint-and-suspicion-model.md §7](/docs/roadmap/0002-admin_console/03-fingerprint-and-suspicion-model.md)):

```text
normalized fingerprint envelope, request IP record, user-agent, referrer,
redirect event, recent visitor history, recent IP history, recent link
history, provider smart signals
```

Output: `risk_score` (0-100), `risk_level` (`low|medium|high|critical`),
`risk_reasons[]`.

## Required risk reasons

Implement at least (
[06.0 README source task](/docs/roadmap/0002-admin_console/06.0-fingerprint-provider-and-suspicion-analyzer/README.md)):

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

## Requirements

1. Rule categories per
   [03-fingerprint-and-suspicion-model.md §7.1](/docs/roadmap/0002-admin_console/03-fingerprint-and-suspicion-model.md):
   provider signal rules, local consistency rules, URL-specific rules.
2. Scoring draft (
   [03-fingerprint-and-suspicion-model.md §8](/docs/roadmap/0002-admin_console/03-fingerprint-and-suspicion-model.md)):
   bad bot +40, anti-detect browser +40, tampering high +35 / medium +20,
   anomaly_score>0.8 +35 / >0.5 +20, VPN/proxy/Tor/datacenter +10 to +25,
   same-IP-many-visitors/same-visitor-many-IPs +20 each, regeneration burst
   +30, rate-limit hit +20, failed-redirect burst +15. Risk levels: 0-24
   low, 25-49 medium, 50-74 high, 75-100 critical.
3. Thresholds and point values must be read from `FingerprintConfig`
   (subtask 01's `risk_thresholds`), not hardcoded, so they are tunable per
   environment.
4. `SuspicionAnalyzer` must be a pure, deterministic function of its inputs
   - no hidden state, no randomness, no wall-clock dependence beyond the
   passed-in `collected_at`/event timestamps - so identical inputs always
   produce identical output (required for reproducible tests).
5. Must run for every fingerprint regardless of which provider produced it,
   including `NoopFingerprintProvider` and `ThumbmarkProvider` results,
   where fewer signals are available and confidence is lower.

## Constraints

- Keep rule evaluation and score summation separable (`SuspicionAnalyzer`
  decides which reasons apply; `RiskScore` sums point values into a level)
  so each is independently testable.

## Success criteria

- [ ] `SuspicionAnalyzer` produces the correct `risk_reasons[]` for each
      documented rule category given a matching fixture input.
- [ ] `RiskScore` maps point totals to the documented risk levels correctly
      at each boundary (24/25, 49/50, 74/75).
- [ ] Identical inputs produce identical output across repeated calls.
- [ ] Risk thresholds are read from config, not hardcoded (verified by a
      test that changes config and observes a different level for the same
      score).
