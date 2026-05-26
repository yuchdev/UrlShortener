# 04 — Admin API Contract

## 1. Base path

All admin API endpoints live under:

```text
/admin/api/v1
```

All endpoints require safe-login session authentication except login.

## 2. Common response conventions

### 2.1 Page response

```json
{
  "items": [],
  "pagination": {
    "page": 1,
    "page_size": 50,
    "total_items": 0,
    "total_pages": 0
  }
}
```

### 2.2 Error response

```json
{
  "error": {
    "code": "not_authorized",
    "message": "The current user is not allowed to perform this action.",
    "request_id": "req_..."
  }
}
```

Common error codes:

```text
not_authenticated
not_authorized
csrf_failed
validation_error
not_found
conflict
rate_limited
internal_error
```

## 3. Authentication endpoints

```http
POST /admin/api/v1/auth/login
POST /admin/api/v1/auth/logout
GET  /admin/api/v1/auth/me
POST /admin/api/v1/auth/change-password
```

`GET /auth/me` response:

```json
{
  "username": "admin",
  "role": "admin",
  "permissions": [
    "analytics:read",
    "url_pairs:read",
    "url_pairs:write",
    "visitor_profiles:read",
    "fingerprints:read",
    "ips:read",
    "console_users:manage",
    "exports:create",
    "audit_log:read"
  ],
  "csrf_token": "..."
}
```

## 4. Analytics endpoints

### 4.1 Overview

```http
GET /admin/api/v1/analytics/overview
```

Query:

```text
from
to
granularity
timezone
```

Response:

```json
{
  "range": {
    "from": "2026-05-20T00:00:00Z",
    "to": "2026-05-20T23:59:59Z",
    "granularity": "hour"
  },
  "totals": {
    "redirects": 128491,
    "successful_redirects": 127900,
    "failed_redirects": 591,
    "unique_visitor_profiles": 21044,
    "unique_fingerprints": 19920,
    "unique_ips": 15102,
    "active_url_pairs": 734,
    "suspicious_events": 92,
    "bot_events": 31,
    "tampering_events": 12,
    "fingerprint_regeneration_attempts": 34
  },
  "series": {
    "redirects": [
      { "bucket": "2026-05-20T00:00:00Z", "count": 4210 }
    ],
    "suspicious_events": [
      { "bucket": "2026-05-20T00:00:00Z", "count": 3 }
    ]
  }
}
```

### 4.2 Link analytics

```http
GET /admin/api/v1/analytics/links
GET /admin/api/v1/analytics/links/{url_pair_id}
GET /admin/api/v1/analytics/links/{url_pair_id}/timeseries
GET /admin/api/v1/analytics/links/{url_pair_id}/referrers
GET /admin/api/v1/analytics/links/{url_pair_id}/visitors
GET /admin/api/v1/analytics/links/{url_pair_id}/events
```

### 4.3 Visitor analytics

```http
GET /admin/api/v1/visitor-profiles
GET /admin/api/v1/visitor-profiles/{visitor_profile_id}
GET /admin/api/v1/visitor-profiles/{visitor_profile_id}/events
GET /admin/api/v1/visitor-profiles/{visitor_profile_id}/fingerprints
GET /admin/api/v1/visitor-profiles/{visitor_profile_id}/ips
```

### 4.4 Fingerprints

```http
GET /admin/api/v1/fingerprints
GET /admin/api/v1/fingerprints/{fingerprint_id}
GET /admin/api/v1/fingerprints/{fingerprint_id}/events
GET /admin/api/v1/fingerprints/{fingerprint_id}/ips
GET /admin/api/v1/fingerprints/{fingerprint_id}/regeneration-history
```

### 4.5 IP addresses

```http
GET /admin/api/v1/ips
GET /admin/api/v1/ips/{ip_id}
GET /admin/api/v1/ips/{ip_id}/events
GET /admin/api/v1/ips/{ip_id}/fingerprints
GET /admin/api/v1/ips/{ip_id}/visitor-profiles
```

### 4.6 Referrers

```http
GET /admin/api/v1/analytics/referrers
GET /admin/api/v1/analytics/referrers/{referrer_domain}
```

### 4.7 Security analytics

```http
GET /admin/api/v1/analytics/security/summary
GET /admin/api/v1/analytics/security/suspicious-events
GET /admin/api/v1/analytics/security/top-ips
GET /admin/api/v1/analytics/security/top-fingerprints
GET /admin/api/v1/analytics/security/link-spikes
```

## 5. URL pair endpoints

```http
GET    /admin/api/v1/url-pairs
GET    /admin/api/v1/url-pairs/{url_pair_id}
PATCH  /admin/api/v1/url-pairs/{url_pair_id}
DELETE /admin/api/v1/url-pairs/{url_pair_id}
```

Write operations require `url_pairs:write`.

## 6. Console user endpoints

```http
GET   /admin/api/v1/console-users
POST  /admin/api/v1/console-users
GET   /admin/api/v1/console-users/{username}
PATCH /admin/api/v1/console-users/{username}
POST  /admin/api/v1/console-users/{username}/reset-password
POST  /admin/api/v1/console-users/{username}/disable
```

All require `console_users:manage`.

## 7. Audit log

```http
GET /admin/api/v1/audit-log
```

Filters:

```text
from
to
console_user
action
target_type
target_id
result
```

## 8. Exports

```http
POST /admin/api/v1/exports/analytics
POST /admin/api/v1/exports/url-pairs
POST /admin/api/v1/exports/redirect-events
GET  /admin/api/v1/exports/{export_id}
```

Export rules:

```text
readonly may export aggregate analytics only
admin may export raw redirect events only if policy allows
all exports are audit-logged
raw IP/fingerprint data must respect masking policy
```

## 9. Public tracking endpoints

These are not admin endpoints, but they are needed for fingerprint enrichment.

```http
POST /api/v1/tracking/fingerprint-event
POST /api/v1/tracking/redirect-beacon
```

These endpoints must be rate-limited and must not require admin authentication.

They must validate:

```text
short_code or redirect_event_id
provider
visitor_id/fingerprint hash format
payload size
collection timestamp freshness
```
