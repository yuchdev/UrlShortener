# Stage 1 Specification — URL Shortener Core

## 1. Stage intent

Build the first **production-usable short-link core** on top of the existing service. This stage should deliver a coherent API and redirect flow that allows clients to:

1. Create short links from long URLs.
2. Retrieve link records by internal ID and by slug.
3. Resolve and redirect from slug to destination URL.
4. Correctly handle invalid, missing, disabled, and expired links while preserving a protected redirect fast path.

This document is written for implementation by another agent and is intentionally explicit about contracts, edge cases, and verification.

---

## 2. Scope and non-goals

### In scope

- New Link domain model and lifecycle fields.
- In-memory repository implementation.
- URL validation + normalization pipeline.
- Slug validation and generation rules.
- Custom alias support.
- Base-domain aware short URL construction.
- Redirect behavior, including permanent vs temporary response selection.
- Unit tests + integration tests.
- Initial redirect micro-benchmarks.
- Developer-facing documentation updates.

### Out of scope (defer)

- Persistent storage (SQLite/Postgres/Redis/etc.).
- Click analytics.
- Rate limiting or abuse protection.
- Authentication/authorization.
- Full observability stack.
- Multi-node consistency.

---

## 3. Architecture requirements

### 3.1 Domain model: `Link`

Create an explicit link entity (language-native struct/class) used across API handlers and storage.

Required fields:

- `id` (string UUIDv4 or ULID; immutable primary identifier)
- `slug` (string, unique, URL-safe, used in redirect path)
- `target_url` (string, normalized absolute URL)
- `short_url` (derived for API responses: `{base_domain}/{slug}`)
- `created_at` (UTC timestamp)
- `updated_at` (UTC timestamp)
- `expires_at` (nullable UTC timestamp)
- `enabled` (bool; default `true`)
- `redirect_type` (enum: `temporary`, `permanent`)

Optional but recommended now for forward compatibility:

- `source` or `metadata` map for future enrichment (can be empty object).

### 3.2 Link state semantics

A link may be:

- **Active**: exists, `enabled=true`, and not expired.
- **Disabled**: exists, `enabled=false`.
- **Expired**: exists, `expires_at` in the past.
- **Missing**: no record for slug/id.

Redirect endpoint behavior must distinguish these states (see §6).

### 3.3 Storage abstraction

Define repository interface and in-memory implementation.

Required repository operations:

- `create(link)`
- `get_by_id(id)`
- `get_by_slug(slug)`
- `slug_exists(slug)`
- `update(link)` (if needed by tests/setup)
- `delete(id)` (optional now, useful for test cleanup)

In-memory backend requirements:

- Use mutex-protected map(s) for thread safety.
- Maintain secondary index for slug uniqueness.
- Deterministic behavior under concurrent create collisions on same custom alias.

### 3.4 Redirect fast path performance contract

The redirect path is the product-critical hot path and MUST remain intentionally simpler than management APIs.

Requirements:

- Treat `GET /{slug}` as the canonical data-plane route for redirect serving.
- Keep management-plane routes under `/api/v1/...` and do not let management-only parsing or feature branching bleed into redirect serving.
- Redirect resolution MUST avoid JSON parsing, management metadata expansion, or synchronous analytics/storage side effects beyond the minimum lookup needed to resolve state.
- Redirect serving MUST remain correct when optional analytics, cache, or observability helpers degrade or are temporarily unavailable.
- Every later stage MUST compare redirect latency against the Stage 1 baseline, including p50/p95/p99 and hot-path hit scenarios.

---

## 4. Configuration contract

Introduce stage-level config block (env vars or existing config system):

- `SHORTENER_BASE_DOMAIN` (required): e.g. `https://sho.rt`
- `SHORTENER_DEFAULT_REDIRECT_TYPE` (optional; `temporary` default)
- `SHORTENER_DEFAULT_EXPIRY_SECONDS` (optional; unset means no expiry)
- `SHORTENER_GENERATED_SLUG_LENGTH` (optional; default `7`)
- `SHORTENER_ALLOW_PRIVATE_TARGETS` (optional; default `false`)

Normalization rules for base domain:

- Must include scheme (`http` or `https`).
- Must not include path/query/fragment.
- Remove trailing slash for canonical internal use.

If base domain invalid at startup, fail fast with clear error.

---

## 5. Validation and normalization rules

### 5.1 Target URL validation

Accepted:

- Absolute URLs with `http://` or `https://`.
- Host required.

Rejected:

- Relative URLs.
- Non-http schemes (`javascript:`, `file:`, `ftp:`, etc.).
- Empty or whitespace-only values.
- Embedded credentials in authority (`https://user:pass@host`).

### 5.1.1 Target destination security policy

This stage MUST make destination policy explicit even if future stages extend it.

Default policy for production-safe behavior:

- Reject loopback targets (`localhost`, `127.0.0.0/8`, `::1`).
- Reject RFC1918/private-network targets unless `SHORTENER_ALLOW_PRIVATE_TARGETS=true`.
- Reject link-local and unspecified-address targets.
- Reject clearly invalid internal-only host placeholders that cannot be safely resolved.
- Apply IDN/punycode handling through the URL parser consistently; do not implement ad-hoc hostname rewriting.

Rationale: even for a URL shortener, target validation is part of abuse resistance and operator safety.

### 5.2 Target URL normalization

Minimum normalization pipeline:

1. Trim surrounding whitespace.
2. Lowercase scheme + host.
3. Remove default port (`:80` for http, `:443` for https`).
4. Preserve path, query, and fragment as provided.
5. Ensure URL re-serialization is deterministic.

Note: Do **not** perform risky canonicalization (like stripping query params) in this stage.

### 5.3 Slug validation

Slug regex for both generated and custom aliases:

- `^[A-Za-z0-9][A-Za-z0-9_-]{2,63}$`

Meaning:

- Length `3..64`.
- First char alphanumeric.
- Subsequent chars alphanumeric, `_`, or `-`.

Reject reserved slugs if applicable to current routing (`api`, `r`, `health`, etc.) via explicit denylist.

### 5.4 Generated slug policy

- Use base62 random generation.
- Default length from config (`7`); keep configurable for future scale.
- On collision, retry up to 20 attempts.
- If still colliding, return internal error code `slug_generation_failed`.
- Benchmark concurrent generated-slug creation to verify collision handling remains bounded under load.

---

## 6. HTTP API specification

> Keep existing endpoints backward compatible unless impossible; add new contract cleanly.

### 6.1 Create short link

- **Method/Path**: `POST /api/v1/links`
- **Request JSON**:
  - required: `url` (string)
  - optional: `slug` (string custom alias)
  - optional: `expires_at` (RFC3339 UTC)
  - optional: `redirect_type` (`temporary`|`permanent`)
  - optional: `enabled` (bool; default true)

Behavior:

- Validate + normalize URL.
- If `slug` provided, validate and enforce uniqueness.
- If `slug` absent, generate unique slug.
- If `redirect_type` absent, use configured default.
- Persist in repository.

Success:

- `201 Created`
- Response body:

```json
{
  "id": "01H...",
  "slug": "abc123",
  "short_url": "https://sho.rt/abc123",
  "url": "https://example.com/path",
  "redirect_type": "temporary",
  "enabled": true,
  "expires_at": null,
  "created_at": "2026-01-01T00:00:00Z",
  "updated_at": "2026-01-01T00:00:00Z"
}
```

Failure statuses:

- `400` invalid input (`invalid_url`, `invalid_slug`, `invalid_expires_at`, `invalid_redirect_type`)
- `409` slug conflict (`slug_conflict`)

### 6.2 Get link by ID

- **Method/Path**: `GET /api/v1/links/id/{id}`
- `200` with same payload shape as create response.
- `404` if not found (`not_found`).

### 6.3 Get link by slug

- **Method/Path**: `GET /api/v1/links/{slug}`
- `200` with same payload shape.
- `404` if not found (`not_found`).

### 6.4 Redirect endpoint

- **Method/Path**: `GET /{slug}`
- Compatibility note: `/r/{slug}` may remain as a temporary alias during migration, but `GET /{slug}` is the canonical redirect route and docs/tests must treat it as such.

Behavior:

- Lookup slug.
- If missing: `404 not_found`.
- If disabled: `410 link_disabled`.
- If expired: `410 link_expired`.
- If active:
  - `redirect_type=temporary` → `302 Found`
  - `redirect_type=permanent` → `301 Moved Permanently`
  - `Location` header set to normalized target URL.

Response body for redirect can be empty.

### 6.5 Error envelope

All shortener endpoints must return consistent machine-readable errors:

```json
{
  "error": {
    "code": "invalid_url",
    "message": "url must be an absolute http/https URL",
    "request_id": "req_123"
  }
}
```

---

## 7. Concurrency and correctness constraints

- Slug uniqueness must hold under concurrent create requests.
- Custom alias race: exactly one request wins, others receive `409`.
- Generated slug collision loop must be bounded and safe.
- Reads should not block each other unnecessarily (favor shared locks if available).

---

## 8. Test plan (mandatory)

### 8.1 Unit tests

Minimum cases:

1. URL validation accepts valid http/https absolute URLs.
2. URL validation rejects invalid/unsafe schemes.
3. URL normalization lowercases scheme+host and strips default ports.
4. Slug validator accepts boundary valid values.
5. Slug validator rejects invalid chars/length.
6. Generated slug collision retry behavior.
7. Expiry evaluation logic.
8. Redirect status selection from redirect type.

### 8.2 Integration tests

Minimum scenarios:

1. Create with generated slug, then resolve redirect successfully.
2. Create with custom slug, then get by ID and by slug.
3. Duplicate custom slug returns `409 slug_conflict`.
4. Disabled link redirect returns configured disabled response.
5. Expired link redirect returns configured expired response.
6. Missing slug returns `404`.
7. Permanent redirect returns `301`; temporary returns `302`.
8. `short_url` includes configured base domain exactly once (no double slashes).

### 8.3 Benchmark baseline

Add an initial benchmark script/doc section reporting:

- redirect requests/sec on local machine
- p50/p95/p99 latency for redirect path
- separate hot-path benchmark (existing active links) and mixed-outcome benchmark (hits, misses, expired/disabled)
- test parameters (concurrency, duration, hardware summary)

No strict performance gate in Stage 1; this is a baseline for future stages.

---

## 9. Documentation deliverables

Update project docs with:

- API reference for all new endpoints.
- Config reference for base domain and defaults.
- Example curl flows:
  - create generated slug
  - create custom alias
  - fetch by id/slug
  - redirect demonstration
- Behavior table for missing/disabled/expired outcomes.
- Explicit note that redirect is the protected fast path and management-plane behavior must not regress it.

---

## 10. Implementation sequence (agent execution plan)

1. Add/extend config model and startup validation for base domain.
2. Implement Link entity + repository interface + in-memory backend.
3. Implement validation/normalization utilities (URL + slug).
4. Implement create endpoint.
5. Implement get-by-id and get-by-slug endpoints.
6. Implement redirect endpoint with status branching.
7. Add uniform error response helpers.
8. Write unit tests, then integration tests.
9. Add benchmark harness and record initial numbers.
10. Update README/docs.

Keep each step in separate commits where practical to simplify review.

---

## 11. Exit criteria mapping

Stage is complete only if all are true:

- Long URL can be shortened and resolved via redirect.
- Generated `short_url` uses configured base domain.
- Missing/disabled/expired link edge cases are implemented and verified by tests.
- Redirect benchmark includes p99 and both hot-path and mixed-outcome scenarios.
- Unit + integration test suites pass in CI/local environment.
- Benchmark baseline results are captured in docs.

---

## 12. Definition of done checklist

- [ ] Link domain model exists and is used by handlers.
- [ ] In-memory repository is thread-safe and enforces slug uniqueness.
- [ ] URL validation + normalization implemented.
- [ ] Slug validation implemented.
- [ ] Custom alias supported with conflict handling.
- [ ] Create/get-by-id/get-by-slug endpoints implemented.
- [ ] Redirect endpoint implemented with 301/302 selection.
- [ ] Missing/disabled/expired outcomes implemented.
- [ ] Error envelope standardized across shortener routes.
- [ ] Unit tests added and passing.
- [ ] Integration tests added and passing.
- [ ] Initial redirect benchmark captured.
- [ ] Docs updated.

---

## Implementation baseline capture (2026-03-25)

### Redirect micro-benchmark

Command:

```bash
scripts/benchmark_redirect.py --base-url http://127.0.0.1:28080 --concurrency 32 --duration 10
```

Reported baseline on local development VM:

- Hot-path (active links): ~11k req/s, p50 2.1 ms, p95 8.8 ms, p99 15.4 ms.
- Mixed outcomes (hit + miss + disabled + expired): ~9k req/s aggregate, p50 ~3.0 ms, p95 ~11 ms, p99 < 20 ms.

Parameters: concurrency=32, duration=10 seconds, Ubuntu 24.04 VM, 8 vCPU.
