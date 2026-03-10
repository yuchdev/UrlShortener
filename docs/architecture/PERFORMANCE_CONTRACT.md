# Performance Contract Specification

## 1. Purpose

This document defines the performance contract for the low-latency URL shortener.
It is intended to guide implementation, code review, benchmarking, and acceptance decisions.

The service does not exist merely to be functionally correct; its central product value is **fast and predictable redirect behavior**.

---

## 2. Performance philosophy

The main performance objective is not maximum synthetic throughput at any cost.
The primary objective is:

- **low median latency**,
- **tight tail latency**,
- **graceful degradation**,
- **stable behavior under realistic production conditions**.

This means design choices must prefer bounded behavior and low variance over clever but fragile micro-optimizations.

---

## 3. Protected fast path

The redirect path is designated as a **protected fast path**.
Any change that touches redirect handling must be evaluated against this contract.

The synchronous redirect path should do only the minimum required work:

1. parse slug
2. read compact redirect record from cache and/or metadata repository
3. evaluate lifecycle state
4. construct redirect or terminal response
5. enqueue analytics best-effort only

The redirect path must not perform unnecessary control-plane work.

---

## 4. Hard rules for redirect handling

### 4.1 Redirect path should avoid

- full JSON parsing/serialization unrelated to response body
- blocking analytics persistence
- loading large management metadata blobs
- per-request dynamic configuration reload
- repeated string transformations that can be precomputed
- synchronous remote lookups beyond metadata/cache resolution
- unbounded retries
- global locks held longer than needed for correctness

### 4.2 Redirect path may do

- cache lookup
- metadata lookup on cache miss
- state evaluation using compact fields
- minimal response construction
- bounded metrics accounting
- best-effort event enqueue

### 4.3 Redirect path must not depend on

- stats aggregation queries
- QR generation
- password prompt rendering pipelines beyond stage-approved minimal checks
- campaign metadata processing unless explicitly required for redirect decision
- any debug-only persistence behavior

---

## 5. Write path principles

Create/update/delete/restore endpoints may be slower than redirects, but they still must remain efficient.
The system must avoid pathological write amplification and redundant serialization work.

Write operations may involve:

- validation
- uniqueness checks
- transactional metadata persistence
- cache invalidation/update
- structured logging

Write operations must not synchronously trigger expensive analytics aggregation or maintenance jobs.

---

## 6. Benchmark classes

The implementation and CI/perf documentation must distinguish the following benchmark classes.

### 6.1 Redirect hot-hit benchmark

All slugs are present in cache or in an equivalent hot local read path.
Purpose: establish best practical redirect latency baseline.

### 6.2 Redirect mixed benchmark

Traffic includes:

- valid active slugs
- misses
- disabled/expired/deleted slugs
- cache misses requiring metadata fallback

Purpose: reflect real service behavior.

### 6.3 Create-link benchmark

Measures create endpoint under realistic slug generation and uniqueness behavior.

### 6.4 Stats benchmark

Measures control-plane reads so that expensive aggregation does not silently regress over time.

### 6.5 Analytics impact benchmark

Measures redirect performance with analytics disabled vs enabled under equivalent load.

---

## 7. Minimum reported metrics

All benchmark reports must include:

- requests/sec
- p50 latency
- p95 latency
- p99 latency
- concurrency level
- request mix
- hardware summary
- backend topology used
- whether TLS was enabled
- whether analytics was enabled

Do not report only averages.
Averages are insufficient for latency-sensitive services.

---

## 8. Initial target guidance

These targets are design guidance and may be tuned once real benchmark baselines exist.
They are not promises for every environment.

For a production-oriented topology with Redis cache and PostgreSQL metadata:

- redirect cache-hit p95 should aim to remain in the low-millisecond range
- redirect cache-hit p99 should remain well below “human-noticeable” thresholds for a shortener
- enabling analytics should not materially distort redirect tail latency under normal queue conditions
- cache outage should degrade latency, not correctness

If absolute numeric SLOs are introduced later, they must be accompanied by explicit hardware and topology assumptions.

---

## 9. Allocation and representation guidance

### 9.1 Compact redirect records

The redirect path should prefer compact in-memory/cache records over general-purpose JSON blobs.
The service must not parse ad hoc JSON text on every redirect if a structured in-memory or typed representation is feasible.

### 9.2 Avoid repeated work

Repeated normalization, parsing, or branching decisions should be moved to create/update time whenever possible.
For example, redirect type should be stored in a directly usable representation.

### 9.3 Limit copying

Avoid unnecessary copies of URLs, headers, and analytics fields.
Truncate and normalize analytics payloads before persistence boundaries.

---

## 10. Degradation requirements

The system must degrade predictably in these scenarios:

### 10.1 Cache unavailable

Expected behavior:

- redirect falls back to metadata backend
- latency increases
- correctness remains intact
- metrics/logging expose cache degradation

### 10.2 Analytics queue saturated

Expected behavior:

- redirect still completes
- event enqueue may drop events according to policy
- memory remains bounded
- warning metrics/logs rise predictably

### 10.3 Metadata backend slower than normal

Expected behavior:

- cache hits remain fast
n- cache misses degrade
- readiness/health behavior reflects degraded backend appropriately
- no hidden retry storms

---

## 11. Code review checklist for performance-sensitive changes

Every PR touching redirect, repository, cache, or analytics enqueue behavior must answer:

1. Does this add synchronous work to redirect handling?
2. Does this add allocations or parsing on the hot path?
3. Does this change cache entry size materially?
4. Does this introduce a new blocking dependency?
5. Does this affect p95/p99 latency expectations?
6. What benchmark or measurement supports the change?

---

## 12. Perf test obligations by stage

### Stage 01

- baseline redirect benchmark
- create/read benchmark
- error-path measurements for misses and inactive links

### Stage 02

- verify management features do not regress redirect fast path
- benchmark preview/stats separately from redirect

### Stage 03

- compare in-memory, SQLite, PostgreSQL, and cached topologies
- measure cache-hit vs cache-miss differences

### Stage 04

- compare analytics enabled/disabled
- overload queue saturation test

### Stage 05

- repeat full benchmark matrix with observability enabled
- preserve benchmark report format for future regressions

---

## 13. Non-goals

This performance contract does not require:

- premature lock-free implementation of every component
- exotic custom allocators unless profiling proves value
- sacrificing clarity of domain logic for unsupported micro-optimizations

What it does require is disciplined avoidance of obvious latency regressions and measurement of meaningful changes.
