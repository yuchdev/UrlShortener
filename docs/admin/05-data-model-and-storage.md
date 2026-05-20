# 05 — Data Model and Storage

## 1. Core analytics principle

Analytics must be based on an immutable redirect-event stream.

Do not calculate all analytics from mutable URL pair rows.

## 2. Redirect event

Conceptual model:

```text
redirect_event
  id
  timestamp
  url_pair_id
  short_code
  target_url_snapshot
  redirect_status
  http_status
  ip_id
  fingerprint_id
  visitor_profile_id
  user_agent
  referrer
  request_path
  query_string_hash
  country
  region
  device_type
  browser
  os
  is_bot
  risk_score
  risk_level
  blocked_reason
  latency_ms
```

## 3. Fingerprint record

```text
fingerprint
  id
  provider
  provider_visitor_id
  fingerprint_hash
  confidence
  first_seen_at
  last_seen_at
  redirect_count
  visitor_profile_id
  current_risk_score
  current_risk_level
  raw_debug_payload_ref
```

## 4. Fingerprint signal record

```text
fingerprint_signal
  id
  fingerprint_id
  redirect_event_id
  provider_request_id
  collected_at
  bot
  bot_type
  incognito
  vpn
  proxy
  tor
  relay
  datacenter
  tampering
  tampering_confidence
  tampering_ml_score
  anomaly_score
  anti_detect_browser
  virtual_machine
  developer_tools
  privacy_settings
  high_activity_device
  provider_suspect_score
```

## 5. IP record

```text
ip_address
  id
  ip_hash
  ip_encrypted_or_plain_if_allowed
  ip_masked_display
  first_seen_at
  last_seen_at
  redirect_count
  current_risk_score
  current_risk_level
```

Store full IP according to project policy.

Recommended:

```text
always store hash for joins
store plain/encrypted IP only if configured
return masked IP to readonly users
```

## 6. Visitor profile

```text
visitor_profile
  id
  primary_fingerprint_id
  first_seen_at
  last_seen_at
  redirect_count
  known_fingerprint_count
  known_ip_count
  regeneration_attempt_count
  current_risk_score
  current_risk_level
```

## 7. Fingerprint regeneration history

```text
fingerprint_regeneration_event
  id
  visitor_profile_id
  old_fingerprint_id
  new_fingerprint_id
  ip_id
  detected_at
  reason
  confidence
```

Reasons:

```text
same_ip_new_fingerprint
same_provider_linked_id
provider_velocity_signal
cookie_reset_suspected
browser_storage_reset_suspected
manual_merge
```

## 8. Aggregates

Create aggregate tables or views for analytics.

Recommended aggregate dimensions:

```text
time bucket
url pair
referrer domain
ip
fingerprint
visitor profile
risk level
bot status
```

Suggested aggregate tables:

```text
analytics_redirects_by_time
analytics_redirects_by_url_pair
analytics_redirects_by_referrer
analytics_redirects_by_ip
analytics_redirects_by_fingerprint
analytics_redirects_by_visitor_profile
analytics_security_summary
```

## 9. SQLite compatibility

If SQLite is used for debug/local mode:

- emulate permission model at application level;
- use indexes aggressively;
- calculate small aggregates on demand;
- optionally materialize aggregate tables periodically;
- keep schema compatible with PostgreSQL where practical.

## 10. PostgreSQL compatibility

If PostgreSQL is used:

- use indexed event table;
- consider materialized views for heavy aggregates;
- use partial indexes for time-window queries;
- use JSONB only for provider-specific debug payloads, not for primary query fields.

## 11. Required indexes

At minimum:

```text
redirect_event(timestamp)
redirect_event(url_pair_id, timestamp)
redirect_event(fingerprint_id, timestamp)
redirect_event(ip_id, timestamp)
redirect_event(visitor_profile_id, timestamp)
redirect_event(risk_level, timestamp)
fingerprint(provider, provider_visitor_id)
fingerprint(visitor_profile_id)
ip_address(ip_hash)
visitor_profile(last_seen_at)
audit_log(timestamp)
```

## 12. Retention

Recommended defaults:

```text
aggregate analytics: keep indefinitely or long-term
redirect events: configurable retention
raw debug fingerprint payloads: short retention only
full IP values: configurable retention/anonymization
masked/hash values: longer retention acceptable
```

All retention settings must be documented.
