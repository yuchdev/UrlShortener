# 03 — Fingerprint and Suspicion Model

## 1. Goal

The URL shortener must collect visitor identity information and detect suspicious/unrealistic fingerprints.

The admin console must expose this data safely through analytics, not as a raw surveillance dump.

## 2. Provider strategy

Use a provider abstraction.

```text
FingerprintProvider
  FingerprintProProvider       production
  ThumbmarkProvider            local/dev fallback
  FingerprintJsOssProvider     secondary fallback
  NoopFingerprintProvider      tests/minimal mode
```

## 3. Production choice

Use **Fingerprint Pro / Fingerprint Identification + Smart Signals** in production.

Required provider capabilities:

```text
stable visitor identifier
request identifier
confidence score
bot detection
incognito/private mode signal
VPN/proxy/relay signal
velocity signals
tampering detection
tampering confidence
tampering ML score
statistical anomaly score
anti-detect-browser detection
virtual machine detection
developer-tools/automation indicator
raw device attributes available only for restricted debug mode
```

## 4. Local/dev fallback

Use ThumbmarkJS or FingerprintJS OSS behind the same interface.

Fallback limitations must be explicit:

```text
browser-only fingerprint is easier to spoof
server cannot fully validate browser-computed values
suspicious detection becomes mostly heuristic
confidence must be lower than production provider
```

## 5. Redirect tracking modes

Because URL shorteners are latency-sensitive, support two collection modes.

### 5.1 Fast redirect mode

The server records:

```text
short code
request timestamp
source IP
user-agent
referrer
headers needed for basic analytics
redirect status
latency
```

Then immediately redirects.

Fingerprint enrichment may be absent.

### 5.2 Enhanced analytics mode

The redirect endpoint returns a minimal HTML page that:

1. starts fingerprint collection;
2. sends fingerprint envelope to backend;
3. redirects to target URL after collection or timeout;
4. enforces a strict maximum delay.

Recommended timeout:

```text
150-300 ms default
configurable per environment
```

If fingerprint collection fails, redirect must still happen unless the link is explicitly configured as protected.

### 5.3 Beacon mode

For selected flows, use `navigator.sendBeacon()` or `fetch(..., { keepalive: true })` to send late analytics data.

Do not depend on beacon success for correctness.

## 6. Normalized fingerprint envelope

Frontend/provider result is normalized before storage.

```ts
export interface ClientFingerprintEnvelope {
  provider: 'fingerprint_pro' | 'thumbmark' | 'fingerprintjs_oss' | 'noop';
  provider_request_id?: string;
  visitor_id?: string;
  fingerprint_hash?: string;
  confidence?: number;
  collected_at: string;
  collection_duration_ms?: number;
  user_agent?: string;
  browser_timezone?: string;
  browser_language?: string;
  screen?: {
    width?: number;
    height?: number;
    color_depth?: number;
    pixel_ratio?: number;
  };
  signals?: FingerprintSignals;
  raw_debug_payload_ref?: string;
}
```

Normalized signals:

```ts
export interface FingerprintSignals {
  bot?: 'not_detected' | 'good' | 'bad' | 'unknown';
  bot_type?: string;
  incognito?: boolean;
  vpn?: boolean;
  proxy?: boolean;
  tor?: boolean;
  relay?: boolean;
  datacenter?: boolean;
  tampering?: boolean;
  tampering_confidence?: 'low' | 'medium' | 'high' | 'unknown';
  tampering_ml_score?: number;
  anomaly_score?: number;
  anti_detect_browser?: boolean;
  virtual_machine?: boolean;
  developer_tools?: boolean;
  privacy_settings?: boolean;
  high_activity_device?: boolean;
  impossible_travel?: boolean;
  provider_suspect_score?: number;
}
```

## 7. Internal suspicion analyzer

Always run an internal `SuspicionAnalyzer` after provider normalization.

The analyzer creates:

```text
risk_score: 0..100
risk_level: low | medium | high | critical
risk_reasons[]
```

### 7.1 Rule categories

#### Provider signal rules

```text
bot == bad
tampering == true
tampering_confidence == high
anomaly_score above threshold
anti_detect_browser == true
virtual_machine == true in suspicious context
developer_tools/automation indicator
vpn/proxy/tor/relay + abusive velocity
```

#### Local consistency rules

```text
same visitor_id with many IPs in short time
same visitor_id with many countries in short time
same IP with many visitor_ids
same IP with many failed redirects
fingerprint regeneration attempts above threshold
browser timezone inconsistent with IP geolocation
user-agent platform inconsistent with client hints
screen/touch/device claims unrealistic for user-agent
very high redirect rate
many short codes accessed in a short interval
```

#### URL-specific rules

```text
one link gets abnormal spike
one visitor accesses many unrelated links
high suspicious percentage for one target domain
high failure rate for one short code
```

## 8. Risk scoring draft

Initial scoring is deterministic and configurable.

```text
bad bot: +40
anti-detect browser: +40
tampering high: +35
tampering medium: +20
anomaly_score > 0.8: +35
anomaly_score > 0.5: +20
VPN/proxy/Tor/datacenter: +10 to +25 depending on context
same IP > N visitor IDs in 1h: +20
same visitor > N IPs in 1h: +20
fingerprint regeneration > N per day: +30
rate-limit hit: +20
failed redirect burst: +15
```

Risk levels:

```text
0-24: low
25-49: medium
50-74: high
75-100: critical
```

## 9. Storage strategy

Store normalized values and risk results.

Do not store full raw fingerprint component payload by default.

If debug raw payload storage is enabled:

```text
store separately
encrypt or restrict access
expire quickly
admin-only
never visible to readonly users
```

## 10. Admin console display

Fingerprint detail page must show:

```text
fingerprint ID / visitor ID
provider
confidence
first seen
last seen
redirect count
associated IPs
associated visitor profile
risk score
risk reasons
provider smart signals
regeneration history
recent redirect events
```

Do not show raw components unless a restricted debug flag is enabled.

## 11. Backend files

Suggested backend files:

```text
src/fingerprint/FingerprintProvider.h
src/fingerprint/FingerprintProvider.cpp
src/fingerprint/FingerprintEnvelope.h
src/fingerprint/FingerprintEnvelope.cpp
src/fingerprint/FingerprintNormalizer.h
src/fingerprint/FingerprintNormalizer.cpp
src/fingerprint/SuspicionAnalyzer.h
src/fingerprint/SuspicionAnalyzer.cpp
src/fingerprint/RiskScore.h
src/fingerprint/RiskScore.cpp
src/fingerprint/providers/FingerprintProProvider.h
src/fingerprint/providers/FingerprintProProvider.cpp
src/fingerprint/providers/ThumbmarkProvider.h
src/fingerprint/providers/ThumbmarkProvider.cpp
src/fingerprint/providers/NoopFingerprintProvider.h
src/fingerprint/providers/NoopFingerprintProvider.cpp
```

Suggested frontend public tracking files:

```text
web/public-tracker/src/fingerprint/FingerprintClient.ts
web/public-tracker/src/fingerprint/providers/FingerprintProClient.ts
web/public-tracker/src/fingerprint/providers/ThumbmarkClient.ts
web/public-tracker/src/fingerprint/FingerprintEnvelope.ts
web/public-tracker/src/redirect/enhancedRedirect.ts
```

If the project already has frontend fingerprint files, adapt names but preserve the provider boundary.
