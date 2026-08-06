# 01 — Product and UX Specification

## 1. Product goal

Create a web admin console that answers these questions:

```text
What links are being used?
Who or what is using them?
Where does traffic come from?
Which links/fingerprints/IPs look suspicious?
What changed recently?
```

The console must be analytics-first. Browsing URL pairs, IPs, fingerprints, and visitor profiles is required, but it must not dominate the experience.

## 2. Main navigation

```text
Dashboard
Analytics
  Overview
  Links
  Visitors
  Fingerprints
  IPs
  Referrers
  Security / Abuse
URL Pairs
Visitor Profiles
Fingerprints
IP Addresses
Console Users
Audit Log
Settings
```

## 3. Top bar

The top bar must include:

```text
Global time range selector
Timezone selector: Local / UTC
Environment label: local/dev/staging/prod
Logged-in console username
Role badge
Logout button
```

Example:

```text
[Last 24h ▼] [Local time ▼]                 dev | admin | Logout
```

## 4. Shared filters

Analytics pages should share a common filter model:

```text
from
to
granularity
short_code
url_pair_id
target_domain
visitor_profile_id
fingerprint_id
ip_id
referrer_domain
risk_level
bot_status
status
```

Filters must be reflected in URL search params so pages can be shared internally.

## 5. Default route

After successful login, users are redirected to:

```text
/admin/dashboard
```

Not to `/admin/url-pairs`.

## 6. Dashboard

### 6.1 Main widgets

```text
Total redirects
Successful redirects
Failed redirects
Unique visitor profiles
Unique fingerprints
Unique IPs
Active URL pairs
Suspicious events
Bot-like events
Fingerprint regeneration attempts
```

### 6.2 Main charts

```text
Redirects over time
Successful vs failed redirects
Suspicious events over time
New vs returning visitors
Top URL pairs
Top referrers
Top suspicious IPs/fingerprints
```

### 6.3 Dashboard layout

```text
┌────────────────┬────────────────┬────────────────┬────────────────┐
│ Redirects      │ Unique Visitors│ Active Links   │ Suspicious     │
└────────────────┴────────────────┴────────────────┴────────────────┘

┌────────────────────────────────────────────────────────────────────┐
│ Redirect volume over time                                          │
└────────────────────────────────────────────────────────────────────┘

┌───────────────────────────────┬────────────────────────────────────┐
│ Top URL pairs                 │ Security / abuse summary           │
└───────────────────────────────┴────────────────────────────────────┘

┌───────────────────────────────┬────────────────────────────────────┐
│ Top referrers                 │ Visitor identity stability         │
└───────────────────────────────┴────────────────────────────────────┘
```

## 7. Analytics pages

### 7.1 Analytics Overview

Route:

```text
/admin/analytics/overview
```

Purpose:

Broad traffic and abuse overview.

Required sections:

```text
traffic volume
unique identity counts
top links
top referrers
top target domains
browser/device distribution
security summary
recent suspicious events
```

### 7.2 Link Analytics

Route:

```text
/admin/analytics/links
```

Required columns:

```text
Short Code
Target URL
Created At
Redirects
Unique Visitors
Unique IPs
Unique Fingerprints
Last Redirect
Suspicious %
Status
```

Link detail route:

```text
/admin/analytics/links/{url_pair_id}
```

Required details:

```text
short code
target URL
status
created/updated timestamps
total redirects
unique visitors
unique fingerprints
unique IPs
redirect timeline
referrers
visitor profiles
recent redirect events
suspicious events
```

### 7.3 Visitor Analytics

Route:

```text
/admin/analytics/visitors
```

Required columns:

```text
Visitor Profile ID
First Seen
Last Seen
Redirects
Unique Links
Fingerprints
IPs
Regeneration Attempts
Risk Score
```

Visitor profile detail route:

```text
/admin/visitor-profiles/{visitor_profile_id}
```

Required sections:

```text
identity summary
associated fingerprints
associated IPs
clicked links
activity timeline
fingerprint changes
IP changes
risk indicators
```

### 7.4 Fingerprint Analytics

Route:

```text
/admin/analytics/fingerprints
```

Required columns:

```text
Fingerprint ID
Provider
First Seen
Last Seen
Redirects
Unique Links
Unique IPs
Visitor Profile
Regeneration Attempts
Risk Score
```

Fingerprint detail route:

```text
/admin/fingerprints/{fingerprint_id}
```

Required sections:

```text
normalized fingerprint summary
provider confidence
smart signals
associated visitor profile
IP history
redirect history
regeneration history
related fingerprints
suspicious signals
```

Do not expose raw fingerprint components by default.

### 7.5 IP Analytics

Route:

```text
/admin/analytics/ips
```

Required columns:

```text
IP or Masked IP
First Seen
Last Seen
Redirects
Unique Links
Unique Fingerprints
Visitor Profiles
Blocked Events
Risk Score
```

IP detail route:

```text
/admin/ips/{ip_id}
```

Required sections:

```text
IP summary
associated fingerprints
associated visitor profiles
redirect timeline
rate-limit events
suspicious events
user-agent distribution
top clicked links
```

### 7.6 Referrer Analytics

Route:

```text
/admin/analytics/referrers
```

Required columns:

```text
Referrer Domain
Redirects
Unique Visitors
Top URL Pair
Suspicious %
Last Seen
```

### 7.7 Security / Abuse Analytics

Route:

```text
/admin/analytics/security
```

Required cards:

```text
Blocked redirects
Rate-limit hits
Suspicious fingerprints
Suspicious IPs
High-frequency visitors
Fingerprint regeneration attempts
Failed redirect attempts
Bot-like events
Tampering events
Anti-detect-browser events
```

Required tables:

```text
top suspicious IPs
top suspicious fingerprints
links with abnormal spikes
many fingerprints from same IP
many IPs for same fingerprint
high failure-rate visitors
high tampering-score events
```

## 8. Secondary browsing pages

### 8.1 URL Pairs

Route:

```text
/admin/url-pairs
```

Required columns:

```text
Short Code
Target URL
Created At
Updated At
Status
Redirect Count
Unique Visitors
Last Redirect
Actions
```

Actions:

```text
View analytics
Copy short URL
Open target
Disable, admin only
Edit, if supported
Delete, only if backend supports safe deletion
```

### 8.2 Visitor Profiles

Route:

```text
/admin/visitor-profiles
```

### 8.3 Fingerprints

Route:

```text
/admin/fingerprints
```

### 8.4 IP Addresses

Route:

```text
/admin/ips
```

### 8.5 Console Users

Route:

```text
/admin/console-users
```

Only visible to admins.

Required actions:

```text
Create user
Disable user
Reset password
Change role
View login history
```

### 8.6 Audit Log

Route:

```text
/admin/audit-log
```

Required events:

```text
login success
login failure
logout
password change
console user creation
console user disable
role change
URL pair modification
export requested
export completed
permission denied
```

## 9. Empty states

Every page must have a useful empty state.

Examples:

```text
No redirect events in selected time range.
No suspicious events detected.
No fingerprints collected yet.
No URL pairs found for current filters.
```

## 10. Error states

Every page must distinguish:

```text
not authenticated
not authorized
network error
backend error
invalid filter
empty result
```
