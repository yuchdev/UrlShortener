# Backend Topology and Deployment Capability Specification

## 1. Purpose

This document defines the intended backend topology for development, debugging, test, and production use.
It complements Stage 03 by making the operational role of each backend explicit.

This document is normative for environment recommendations and for agentic implementation decisions involving backend defaults.

---

## 2. Backend roles at a glance

| Backend | Primary role | Intended environments | Source of truth | Notes |
|---|---|---|---|---|
| In-memory | fast local execution, tests | unit tests, simple local runs | no | volatile only |
| SQLite | local durable development backend | local dev, debug, demos, single-node smoke | limited | not the primary production recommendation |
| PostgreSQL | durable metadata store | staging, production | yes | canonical metadata authority |
| Redis | hot-path cache, optional rate-limit primitives | staging, production | no | must not become the only source of metadata truth |
| Analytics DB/repository | click event persistence and aggregate reads | staging, production | yes for analytics | separate from metadata authority |

---

## 3. Authoritative data ownership

### 3.1 Link metadata authority

For production and staging, **PostgreSQL is the canonical source of truth for link metadata**.
This includes:

- slug ownership
- target URL
- lifecycle state
- redirect type
- timestamps
- management metadata

Redis may cache a projection of this data, but cached data is derivative.
If cache state disagrees with PostgreSQL, PostgreSQL wins.

### 3.2 Analytics authority

Analytics data becomes authoritative only when Stage 04 analytics persistence is enabled and configured.
Stage 02 counters are provisional and must not be treated as durable truth.

### 3.3 Local development authority

For local development, SQLite may act as a practical metadata authority when PostgreSQL is not provisioned.
This is acceptable only for local or explicitly documented single-node scenarios.

---

## 4. Supported environment profiles

### 4.1 Unit test profile

Recommended components:

- metadata: in-memory repository
- cache: null cache or in-memory fake
- analytics: in-memory fake / disabled

Goals:

- deterministic tests
- low startup cost
- no external services required

### 4.2 Integration test profile

Recommended components:

- metadata: in-memory and/or SQLite; optionally PostgreSQL in CI integration jobs
- cache: null cache or ephemeral Redis when required
- analytics: in-memory fake or dedicated test repository

Goals:

- endpoint verification
- backend contract coverage
- migration checks where relevant

### 4.3 Developer debug profile

Recommended components:

- metadata: SQLite by default, PostgreSQL optional
- cache: null cache by default; Redis optional
- analytics: disabled or local persistent test backend

Goals:

- easy setup
- inspectable local state
- reproducible debugging

### 4.4 Production profile

Recommended components:

- metadata: PostgreSQL
- cache: Redis
- analytics: dedicated repository configured per Stage 04
- observability: structured logs + metrics enabled
- TLS: enabled for public traffic where applicable

Goals:

- durable correctness
- predictable latency
- observable degradation behavior

---

## 5. Redirect read path

The default production redirect path must be:

```text
request
  -> parse slug
  -> cache lookup (Redis)
  -> if hit: evaluate state and respond
  -> if miss: metadata lookup (PostgreSQL)
  -> populate/refresh cache
  -> evaluate state and respond
  -> enqueue analytics best-effort
```

Rules:

- cache miss must not be treated as error
- cache unavailability must not fail redirect if metadata backend is reachable
- analytics persistence must not be on the synchronous path
- no control-plane-only metadata should be required to issue redirect

---

## 6. Write path expectations

The canonical mutation order for metadata-changing operations is:

1. validate input
2. persist mutation to authoritative metadata store
3. invalidate or update cache deterministically
4. return response

Redis must not be written as the only durable record of metadata mutations.

---

## 7. Cache topology rules

### 7.1 Cache purpose

Redis is used to reduce redirect lookup latency and relieve primary metadata store load.
It is not used to redefine business rules.

### 7.2 Cache contents

Cache entries should contain a compact redirect-focused projection, for example:

- slug
- target URL
- redirect type
- enabled flag
- expiration/deletion indicators required for redirect state evaluation
- version/updated-at field when implemented

Management-only metadata should not be required in redirect cache records unless justified.

### 7.3 Cache consistency strategy

Preferred strategy:

- cache-aside on read
- invalidate-on-write for state-changing mutations
- optional eager warm on create
- TTL configured conservatively
- optional version check when supported

### 7.4 Degraded mode

If Redis is unavailable:

- redirect path falls back to metadata repository
- service emits metrics/logging for cache degradation
- correctness is preserved even if latency worsens

---

## 8. SQLite role and limitations

SQLite is useful for:

- local development
- reproducible debugging
- simple demos
- migration validation
- single-node experiments

SQLite is not the default recommendation for horizontally scaled production deployment because:

- it is file-based
- multi-instance synchronization is outside scope
- cache invalidation across nodes becomes non-trivial
- operational scaling constraints differ from PostgreSQL

If SQLite is used in production-like conditions, the limitations must be explicitly documented in deployment materials.

---

## 9. PostgreSQL role and expectations

PostgreSQL is the preferred durable metadata backend for staging and production because it provides:

- transactional write semantics
- strong uniqueness enforcement
- mature indexing
- well-understood operational tooling
- multi-client concurrency behavior

Minimum expectations:

- unique index/constraint on slug
- migrations managed and versioned
- connection pool sizing documented
- query plan conscious schema/index design

---

## 10. Redis role and expectations

Redis may be used for:

- redirect lookup cache
- optional rate limit counters/token buckets
- optional short-lived coordination where documented

Redis must not be required for correctness of create/read/update/delete management operations unless explicitly documented for a separate feature.

---

## 11. Analytics storage topology

Analytics storage must be logically separate from redirect metadata authority.
The exact repository may be PostgreSQL or another stage-approved backend, but the architecture must preserve these rules:

- redirect outcome capture is best-effort
- analytics backlog must be bounded in memory
- analytics write failures must not block redirect completion
- aggregate reads belong to the control plane

---

## 12. Secrets and configuration placement

The following configuration items are sensitive and must be injected through secure configuration channels appropriate for the environment:

- PostgreSQL credentials
- Redis credentials
- TLS private key paths or secret references
- HMAC/pepper secrets for anonymized analytics identity derivation
- any mTLS private material

Secrets must not be hard-coded in source or committed example configs with live values.

---

## 13. Example supported topologies

### 13.1 Minimal local developer topology

```text
simple-http
  + SQLite metadata
  + no Redis
  + analytics disabled
```

### 13.2 Realistic staging topology

```text
simple-http
  + PostgreSQL metadata
  + Redis cache
  + analytics repository enabled
  + structured logs
  + metrics enabled
```

### 13.3 Production baseline topology

```text
N application instances
  + shared PostgreSQL metadata
  + shared Redis cache
  + dedicated analytics persistence
  + TLS termination or end-to-end TLS as deployment requires
  + central log and metrics collection outside app scope
```

---

## 14. Implementation guardrails derived from topology

Autonomous implementation must respect the following:

1. Do not treat all backends as equally production-capable.
2. Do not make Redis mandatory for correctness.
3. Do not make SQLite the recommended production default.
4. Do not bypass PostgreSQL uniqueness/integrity features when PostgreSQL is selected.
5. Do not intertwine analytics persistence with metadata transactions.

---

## 15. Documentation obligations

When a backend capability changes, the following documents must be updated together:

- this topology document
- deployment/configuration docs
- CI/test matrix docs when affected
- any migration/runbook documents for operators
