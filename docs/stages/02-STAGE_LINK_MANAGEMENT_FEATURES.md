# Stage 2 Specification — Link Management Features for Real-World Shorteners

## 1. Stage intent

Evolve the current short-link API from basic create/resolve behavior into a **manageable lifecycle service** suitable for production operations.

This stage defines API and domain-level behavior for link governance features (state toggles, soft delete, expiry, metadata, preview, and aggregated stats) while explicitly avoiding persistence-layer rewrites. The stage must preserve clear separation between the management/control plane and the redirect/data plane.

---

## 2. Scope and constraints

### 2.1 In scope

- Enable/disable lifecycle controls.
- Soft deletion (non-destructive removal from active serving).
- Expiration policy support.
- Tagging and metadata fields.
- Campaign metadata fields.
- Preview endpoint.
- Basic aggregate statistics endpoint.
- Reserved slug protection.
- Hook points for future password protection and rate limiting.
- Tests covering management lifecycle and state transitions.
- Documentation and migration notes for API/data-shape evolution.

### 2.2 Out of scope (must defer)

- Any storage abstraction redesign.
- Any database schema migration execution.
- New storage backends.
- Full analytics/event pipeline.
- Full password and rate-limiting implementation.
- QR code generation implementation.

### 2.3 Hard constraint

Do **not** modify repository/storage interface contracts or concrete database adapters in this stage. If new fields are introduced in domain/API models, map them through existing persistence mechanisms in a backward-compatible way (e.g., optional JSON fields).

### 2.4 Control plane vs data plane

This stage MUST keep management richness away from the redirect hot path.

- **Control plane**: create, patch, enable/disable, delete/restore, preview, stats, and future admin operations under `/api/v1/...`.
- **Data plane**: redirect resolution only (`GET /{slug}` and any temporary compatibility alias).
- Management-only fields such as tags, metadata, campaign data, and preview/stats shape MUST NOT be required to resolve a redirect.
- Implementations SHOULD keep a minimal redirect-serving view of a link containing only slug, target URL, redirect type, enabled flag, and lifecycle timestamps needed for state resolution.

---

## 3. Domain model updates

Extend existing `Link` resource semantics with management fields.

### 3.1 Required fields (new or clarified)

- `enabled` (bool, default `true`)
- `deleted_at` (nullable timestamp; non-null indicates soft-deleted)
- `expires_at` (nullable timestamp; already present or newly required)
- `tags` (array of strings, default `[]`)
- `metadata` (object map, default `{}`)
- `campaign` (object, nullable)
  - `campaign.name` (string, optional)
  - `campaign.source` (string, optional)
  - `campaign.medium` (string, optional)
  - `campaign.term` (string, optional)
  - `campaign.content` (string, optional)
  - `campaign.id` (string, optional)
- `stats` (read model only for this stage; provisional until Stage 04 authoritative analytics; see §7)

### 3.2 Derived state model

A link status must resolve deterministically in this precedence:

1. `deleted_at != null` → `deleted`
2. `enabled == false` → `disabled`
3. `expires_at != null && now >= expires_at` → `expired`
4. otherwise → `active`

Only `active` links are redirectable.

### 3.3 Field constraints

- `tags`
  - max 20 entries
  - each tag length 1..32
  - regex: `^[a-zA-Z0-9:_-]+$`
  - normalize by trim + deduplicate (case-sensitive unless current API policy is case-insensitive; choose one and document)
- `metadata`
  - flat object only (no nested objects for stage simplicity)
  - max 50 keys
  - key length max 64; value length max 512
- `campaign`
  - each field max length 128
  - no validation of attribution semantics beyond length/type

---

## 4. API contract changes

> Keep existing endpoints backward compatible. Add/extend management routes under the existing versioned API namespace.

### 4.1 Link representation

All read responses returning a link should include:

```json
{
  "id": "01H...",
  "slug": "promo-2026",
  "short_url": "https://sho.rt/promo-2026",
  "url": "https://example.com/landing",
  "enabled": true,
  "deleted_at": null,
  "expires_at": "2026-04-01T00:00:00Z",
  "tags": ["spring", "paid"],
  "metadata": {
    "owner": "growth-team",
    "locale": "en-US"
  },
  "campaign": {
    "name": "spring-launch",
    "source": "newsletter",
    "medium": "email",
    "term": "spring",
    "content": "hero-a",
    "id": "cmp_123"
  },
  "created_at": "2026-03-01T10:00:00Z",
  "updated_at": "2026-03-10T12:00:00Z"
}
```

### 4.2 Create link (extended)

- **Method/Path:** existing create endpoint.
- Accept optional fields on create:
  - `enabled`
  - `expires_at`
  - `tags`
  - `metadata`
  - `campaign`
- Reject attempts to set `deleted_at` directly.

New errors:

- `invalid_tags`
- `invalid_metadata`
- `invalid_campaign`

### 4.3 Update management fields

Introduce partial update endpoint.

- **Method/Path:** `PATCH /api/v1/links/{slug}` (or by id if current API is id-centric; select one canonical route and keep alias route optional)
- Allowed patchable fields:
  - `enabled`
  - `expires_at`
  - `tags`
  - `metadata`
  - `campaign`
- Server-managed fields:
  - `updated_at` always refreshed
  - `deleted_at` only via delete/restore operations

Status codes:

- `200` success with updated link payload
- `400` validation failure
- `404` not found
- `409` state conflict (optional for invalid transition policies)

### 4.4 Enable/disable shortcut endpoints (optional but recommended)

For explicit operational ergonomics:

- `POST /api/v1/links/{slug}/enable`
- `POST /api/v1/links/{slug}/disable`

Both return `200` with current link payload and are idempotent.

### 4.5 Soft delete endpoint

- **Method/Path:** `DELETE /api/v1/links/{slug}`
- Behavior:
  - mark `deleted_at=now`
  - keep underlying record retrievable via management read endpoint unless policy says hidden
  - redirect path must behave as not active (recommended response `404 not_found` to avoid existence leaks)

Restore endpoint (recommended):

- `POST /api/v1/links/{slug}/restore`
- Sets `deleted_at=null`.

### 4.6 Preview endpoint

- **Method/Path:** `GET /api/v1/links/{slug}/preview`
- Purpose: resolve destination metadata without redirect.

Response example:

```json
{
  "slug": "promo-2026",
  "url": "https://example.com/landing",
  "status": "active",
  "redirect_type": "temporary",
  "enabled": true,
  "expires_at": "2026-04-01T00:00:00Z",
  "deleted_at": null
}
```

For non-active links return `200` with `status` (`disabled|expired|deleted`) unless existing security policy requires `404` for deleted.

### 4.7 Aggregate statistics endpoint

Stage 2 statistics are provisional operational counters only. They are useful for management/debug flows, but they are not yet the authoritative analytics model. Stage 04 becomes the first authoritative analytics/event pipeline stage.


- **Method/Path:** `GET /api/v1/links/{slug}/stats`
- Stage-2 scope is **basic aggregates only**:
  - `total_redirects`
  - `last_accessed_at` (nullable)
  - `created_at`
  - optional coarse buckets: `redirects_24h`, `redirects_7d`

Response example:

```json
{
  "slug": "promo-2026",
  "total_redirects": 1284,
  "redirects_24h": 42,
  "redirects_7d": 301,
  "last_accessed_at": "2026-03-10T11:59:00Z"
}
```

If stats are unavailable in current storage layer, return deterministic zeros/nulls rather than omitting fields. Documentation MUST state whether values reset on process restart or persist through the current backend.

### 4.8 Reserved slug protection

Validate slug creation/updates against denylist.

Required minimum reserved set:

- `api`, `health`, `metrics`, `admin`, `login`, `logout`, `r`, `preview`, `stats`

Behavior:

- create/update with reserved slug → `409 reserved_slug`
- matching should be case-insensitive

---

## 5. Redirect behavior updates

Redirect endpoint must apply lifecycle gates before issuing 3xx:

1. deleted → `404 not_found` (recommended)
2. disabled → `410 link_disabled`
3. expired → `410 link_expired`
4. active → `301/302` based on configured redirect type

On successful redirect, increment aggregate counters through an existing telemetry/update path compatible with current storage constraints. This update MUST remain lightweight and MUST NOT introduce synchronous heavyweight work into redirect serving.

---

## 6. Security and extensibility hooks

Implement no-op but wired extension points in request pipeline.

### 6.1 Password protection hook

Before redirect:

- Call `PasswordGuard::check(link, request)` (or equivalent interface).
- Default implementation always allows.
- Future stages can enforce password/token with no handler rewrites.

### 6.2 Rate limiting hook

Before redirect:

- Call `RateLimitGuard::allow(link, request_context)`.
- Default implementation always allows and emits pass-through result.

### 6.3 Deferred extensions

QR rendering and advanced routing policies are explicitly deferred. Do not add runtime branching or endpoint surface for them in this stage unless another document depends on a stable placeholder route. If a placeholder is still required for compatibility planning, it MUST return a stable `501 feature_not_enabled` response and remain fully outside the redirect hot path.

---

## 7. Statistics model (basic)

Because storage abstractions cannot change in this stage, use one of:

- existing per-link mutable JSON with optional stat fields, or
- in-memory runtime counters with documented reset semantics.

Mandatory semantics:

- counter increments on successful redirect only.
- stats endpoint is eventually consistent if async increment path is used.
- service restart behavior must be documented (persisted or reset).
- stats MUST be labeled as provisional in docs and API notes until Stage 04 is implemented.

---

## 8. Validation and error envelope

Retain existing machine-readable error envelope:

```json
{
  "error": {
    "code": "...",
    "message": "..."
  }
}
```

Add standard codes:

- `reserved_slug`
- `link_deleted`
- `link_disabled`
- `link_expired`
- `invalid_tags`
- `invalid_metadata`
- `invalid_campaign`
- `feature_not_enabled`

---

## 9. Test plan (mandatory)

### 9.1 Unit tests

Minimum set:

1. status precedence resolution (`deleted > disabled > expired > active`)
2. tag validation (bounds, regex, dedupe)
3. metadata constraints (key/value limits)
4. campaign field validation
5. reserved slug matching (case-insensitive)
6. hook default behavior (password/rate-limit pass)
7. stats increment logic on successful redirects only

### 9.2 Integration tests

Minimum scenarios:

1. create with tags/metadata/campaign and read back fields
2. disable link then redirect returns disabled response
3. re-enable link then redirect succeeds
4. soft delete link then redirect blocked and management read behavior verified
5. restore soft-deleted link and redirect works
6. expired link redirects blocked
7. preview endpoint returns status for active/disabled/expired/deleted
8. stats endpoint reports deterministic structure and counter updates
9. reserved slug create attempt returns `409 reserved_slug`
10. backward compatibility: prior create/read/redirect flows still pass

### 9.3 Non-functional checks

- concurrency check for rapid enable/disable toggles (last write wins or defined policy)
- idempotency checks for enable/disable/delete/restore commands

---

## 10. Migration and rollout notes

Even without changing storage abstractions, document data-shape evolution.

Required migration notes section in docs:

- newly introduced optional fields and defaults when absent.
- how old records are interpreted (`enabled=true`, `deleted_at=null`, `tags=[]`, etc.).
- whether stats start from zero for existing links.
- API compatibility statement for old clients.

Rollout recommendation:

1. deploy read-path tolerant parser first (accept missing new fields)
2. enable write-path for new fields
3. enable lifecycle endpoints
4. announce preview/stats endpoints

---

## 11. Documentation deliverables

Update docs with:

- full API reference for new/extended endpoints
- lifecycle state table
- error-code matrix
- curl examples for disable/delete/restore/preview/stats
- reserved slug policy
- extensibility hook behavior and current no-op status

---

## 12. Implementation sequence (execution plan)

1. Extend link DTO/domain model with optional management fields + defaults.
2. Add validation for tags/metadata/campaign and reserved slugs.
3. Implement lifecycle transitions (enable/disable/soft delete/restore).
4. Add preview endpoint.
5. Add basic stats read/update behavior compatible with current storage.
6. Insert password/rate-limit hook interfaces with default pass-through implementations.
7. Add QR/routing extension stubs and validation.
8. Add/update unit and integration tests.
9. Update docs + migration notes.

---

## 13. Exit criteria mapping

Stage is complete only if all are true:

- Links can be enabled/disabled, soft-deleted/restored, and expired by policy.
- Tags/metadata/campaign fields are accepted, validated, and returned.
- Preview and basic aggregate stats endpoints are available.
- Reserved slugs are protected.
- Password/rate-limit/QR/routing extension points are wired (even if no-op).
- No storage abstraction/database redesign was introduced.
- Lifecycle and management tests pass.
- Documentation and migration notes are published.

---

## 14. Definition of done checklist

- [ ] Management fields added with safe defaults.
- [ ] Redirect path enforces lifecycle state gating.
- [ ] Enable/disable endpoints (or equivalent patch semantics) implemented.
- [ ] Soft delete and restore flows implemented.
- [ ] Preview endpoint implemented.
- [ ] Aggregate stats endpoint implemented.
- [ ] Reserved slug protection implemented.
- [ ] Password/rate-limit hooks added with default no-op strategy.
- [ ] QR and advanced routing extension points introduced.
- [ ] Unit and integration tests added and passing.
- [ ] Migration notes documented.
- [ ] API docs updated.
