# 06 — Security, Privacy, and Permissions

## 1. Safe login

The React admin console must use the safe login system implemented previously.

Required properties:

```text
server-side session
HttpOnly cookie
Secure cookie in HTTPS
SameSite=Lax or Strict
CSRF token for mutating requests
backend authorization middleware
no browser-side password hash exposure
no browser-side direct DB access
```

## 2. Console roles

### Admin

Can:

```text
view all analytics
view URL pairs
modify/disable URL pairs if backend supports it
view fingerprints
view IPs according to policy
view visitor profiles
manage console users
export data according to policy
view audit log
change admin settings
```

### Read-only

Can:

```text
view dashboard
view aggregate analytics
view URL pairs
view visitor profiles/fingerprints/IPs in masked form
export aggregate analytics if enabled
```

Cannot:

```text
create/disable users
change roles
modify URL pairs
export raw redirect events
view raw fingerprint components
view full IPs unless explicitly allowed
```

## 3. Permissions

Use permissions in addition to roles.

Initial permissions:

```text
analytics:read
url_pairs:read
url_pairs:write
visitor_profiles:read
fingerprints:read
ips:read
ips:read_full
console_users:manage
exports:create_aggregate
exports:create_raw
audit_log:read
settings:read
settings:write
```

## 4. Frontend permission behavior

The frontend must:

- hide unavailable controls;
- avoid fetching data the current user cannot see;
- show useful not-authorized messages;
- not rely on frontend checks for security.

Backend must reject unauthorized requests.

## 5. IP display policy

Recommended:

```text
admin + ips:read_full -> full IP if storage policy allows
admin without ips:read_full -> masked IP
readonly -> masked IP
```

Examples:

```text
203.0.113.42      full
203.0.113.xxx     masked IPv4
2001:db8:abcd::/48 masked IPv6
```

## 6. Fingerprint display policy

Show by default:

```text
fingerprint internal ID
provider name
provider confidence
risk score
risk reasons
associated IP count
associated visitor profile
first/last seen
```

Hide by default:

```text
raw canvas/audio/font/WebGL data
full provider raw payload
raw client components
```

## 7. Audit log rules

Audit these events:

```text
login success
login failure
logout
password change
console user creation
console user disable
role change
permission denied
URL pair mutation
export request
export completion
settings change
```

Audit record fields:

```text
timestamp
console_user
action
target_type
target_id
source_ip_hash
result
request_id
metadata_json
```

## 8. CSRF

All mutating endpoints require CSRF token.

Mutating methods:

```text
POST
PATCH
PUT
DELETE
```

## 9. Security headers

Serve admin console with:

```text
Content-Security-Policy
X-Content-Type-Options: nosniff
Referrer-Policy: same-origin or strict-origin-when-cross-origin
Permissions-Policy
Frame-Options or CSP frame-ancestors
```

CSP must account for the selected fingerprint provider if used on admin pages. Prefer not to run public fingerprint tracking scripts inside admin pages unless explicitly needed.

## 10. Public tracking endpoint protection

Public tracking endpoints must:

```text
rate-limit by IP and event id
reject oversized payloads
validate provider values
reject stale timestamps
avoid reflecting raw input in errors
not expose admin data
```

## 11. Privacy note

Fingerprinting and IP analytics may be regulated depending on deployment region and use case.

The project must support:

```text
privacy policy text/config hook
retention settings
masking/anonymization
raw payload disable switch
export controls
```
