# Security Model and Input Policy Specification

## 1. Purpose

This document defines the baseline security model for the low-latency URL shortener.
It supplements the stage specifications by making explicit the expected security boundaries, validation rules, and operational assumptions.

The service is a public-facing redirector and therefore must assume hostile input.

---

## 2. Security objectives

The implementation must aim to:

1. reject malformed and unsafe input early
2. prevent obvious abuse via unsafe target schemes or internal targets where policy forbids them
3. preserve integrity of stored link metadata
4. avoid secret leakage in logs and responses
5. preserve redirect correctness under attack-like malformed input
6. keep security controls compatible with low-latency serving

---

## 3. Trust boundaries

### 3.1 Client boundary

All incoming HTTP/HTTPS requests are untrusted.
This includes:

- request line/path
- query parameters
- headers
- body content
- claimed content type
- claimed user agent/referrer values

### 3.2 Configuration boundary

Configuration loaded from files/environment/CLI is trusted only after successful validation.
Misconfiguration must fail fast where correctness or security would otherwise be ambiguous.

### 3.3 Backend boundary

Metadata stores, caches, and analytics repositories are trusted infrastructure components but may fail, degrade, or return errors.
Application logic must treat failures as operational events, not as proof of malicious compromise.

---

## 4. URL target policy

### 4.1 Allowed schemes

By default, only the following target schemes are allowed:

- `http`
- `https`

### 4.2 Disallowed schemes

The service must reject at create/update time any target URL using schemes such as:

- `javascript:`
- `data:`
- `file:`
- `ftp:` unless explicitly approved by policy in future
- custom application schemes unless explicitly approved by policy in future

### 4.3 Absolute URL requirement

Targets must be absolute URLs with a present host component.
Relative URLs are not allowed as redirect destinations.

### 4.4 Credential-bearing URLs

URLs embedding user credentials (`user:pass@host`) should be rejected by default unless a future policy explicitly allows them.
They create operational and privacy risks.

---

## 5. Internal and special-address policy

The implementation must support explicit policy decisions for the following destination categories:

- loopback (`127.0.0.0/8`, `::1`, `localhost`)
- private IPv4 ranges (RFC1918)
- link-local ranges
- unique local IPv6 ranges
- multicast/broadcast where applicable
- internal-only hostnames/suffixes if organization-specific policy exists

### 5.1 Default recommendation

For internet-facing deployments, the default recommended policy is to **reject internal/private/loopback destination targets** unless there is a documented internal-use case.

### 5.2 Policy configurability

The implementation may expose a configurable allow/deny policy, but the effective policy must be easy to document and test.
Do not implement vague “best effort” network classification.

---

## 6. Slug/input policy

### 6.1 Slug character set

Slug policy should be narrow and explicit.
Unless changed by stage specification, use a documented safe character set such as URL-friendly base62 plus optional approved separators.

### 6.2 Slug length

Minimum and maximum slug lengths must be enforced consistently.
Overly long slugs must be rejected before repository access.

### 6.3 Reserved slugs

Reserved routing prefixes and paths must be rejected as custom slugs.
Examples include:

- `api`
- `health`
- `metrics`
- `ready`
- future administrative prefixes

### 6.4 Metadata limits

Metadata, tag, and campaign fields must have explicit size limits.
The control plane should reject oversized payloads rather than truncate silently unless truncation behavior is explicitly documented.

---

## 7. Request size and parsing limits

The implementation must enforce explicit transport-level limits for:

- maximum request body size
- maximum JSON document size
- maximum number of metadata/tag entries if structured fields exist
- maximum header size if supported by transport layer controls
- maximum path length accepted by the router

Malformed JSON, invalid UTF-8 where relevant, and content-type mismatch should produce stable client errors.

---

## 8. Logging and privacy rules

### 8.1 Never log secrets

The system must not log:

- raw private keys
- passwords
- database credentials
- raw HMAC pepper/secret values
- full sensitive headers when not needed

### 8.2 Careful logging of URLs

Destination URLs are product data and may still contain sensitive query fragments.
Logs should avoid indiscriminate full-value dumping in high-volume paths.
Prefer structured logging with selective fields and redaction options.

### 8.3 Analytics identifiers

When Stage 04 analytics uses anonymized client identifiers, the derivation secret must be treated as sensitive configuration.
Rotation procedures must be documented.

---

## 9. TLS and transport security

The service already uses TLS capabilities in current architecture.
Implementation must preserve and not weaken:

- minimum TLS version controls
- secure ciphers/curves controls supported by existing architecture
- certificate/key loading validation
- safe reload behavior if SIGHUP/live reload is supported
- optional mTLS behavior where configured

Transport security configuration must fail clearly on invalid certificate paths or malformed cryptographic material.

---

## 10. Injection resistance

### 10.1 SQL injection

All SQL-backed repositories must use parameterized queries or equivalent safe APIs.
String interpolation for SQL statements with user-controlled values is forbidden.

### 10.2 Log injection

Structured logs should avoid direct inclusion of raw control characters from request data where log sinks could be confused.
When needed, sanitize or encode values.

### 10.3 Header/value reflection

Error responses and preview endpoints must not reflect untrusted content in a way that introduces header splitting or unsafe HTML rendering assumptions.
The service is JSON/API-first and should keep outputs plain and controlled.

---

## 11. Abuse and rate-limit integration points

Abuse mitigation may evolve over stages, but the architecture must leave room for:

- request rate limits
- create endpoint throttling
- destination allow/deny checks
- administrative disabling of abusive links
- future malware/domain reputation checks if desired

The absence of advanced abuse tooling does not justify insecure URL acceptance or missing input limits.

---

## 12. Failure behavior requirements

Security-related validation failures must:

- fail closed where applicable
- return stable client error categories
- avoid ambiguous partial writes
- emit actionable logs without leaking secrets

Backend outages are operational failures, not validation failures, and must be reported separately.

---

## 13. Security test obligations

The test suite must include coverage for:

- disallowed URL schemes
- missing/invalid host
- malformed URLs
- private/loopback targets according to policy
- reserved slug rejection
- oversized bodies/metadata
- malformed JSON
- duplicate slug conflicts
- SQL-backed repositories using safe parameterization paths
- request/response logging redaction where applicable

Stage 05 should extend with fuzz and malformed-input hardening tests.

---

## 14. Operational notes

Security posture depends on deployment context.
Documentation must state at least:

- whether the deployment is public-facing or internal-only
- the target URL allow/deny policy in effect
- whether mTLS is enabled
- how secrets are provisioned
- how certificate rotation/reload is performed

---

## 15. Non-goals

This security model does not attempt to deliver:

- enterprise IAM/RBAC design for all administrative functions
- full browser-facing HTML security model
- malware-scanning platform inside the shortener itself

It does require a disciplined baseline suitable for a public-facing redirect service.
