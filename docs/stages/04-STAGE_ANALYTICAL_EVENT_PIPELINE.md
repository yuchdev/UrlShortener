# Stage 04 Technical Specification

## Add Analytics/Event Pipeline

## 1. Purpose and Scope

This specification defines Stage 04 implementation for a redirect analytics/event pipeline that captures click activity while preserving fast, non-blocking redirect handling.

The stage covers:
- click event model definition
- asynchronous event enqueue from redirect flow
- collection of core click fields (timestamp, slug/link identifier, referrer, user agent, anonymized client identifier, status code, domain)
- aggregate stats read endpoint
- optional future analytics sink interface for external pipelines
- retention and cleanup hooks for event data lifecycle management
- test coverage for event generation and aggregation
- documentation of privacy/anonymization assumptions

Out of scope:
- product analytics dashboards/UI
- raw personally identifiable data retention
- guaranteed exactly-once delivery to external systems

---

## 2. Goals and Non-Goals

### 2.1 Goals
1. Redirect hot path MUST remain non-blocking with respect to analytics persistence.
2. Every redirect attempt SHOULD produce a normalized click event with required fields. This stage measures redirect attempts/outcomes; successful redirects are a subset of those attempts.
3. Analytics pipeline failures MUST NOT fail the redirect response.
4. Aggregated stats endpoint MUST return correct counters and time-bucket summaries for a slug/link.
5. Privacy defaults MUST store anonymized client identifier instead of raw client IP.
6. Retention behavior MUST be configurable and safe by default.

### 2.2 Non-Goals
1. Building long-term data warehouse integrations in this stage.
2. Real-time streaming analytics with strict delivery SLAs.
3. Full user/session attribution features.

---

## 3. High-Level Architecture

Introduce an internal event pipeline with bounded asynchronous processing:

1. Redirect handler resolves slug to target and status.
2. Handler builds `ClickEvent` payload and submits it to an in-memory bounded queue.
3. Queue push is best-effort and non-blocking (drop-on-overflow policy with metrics/logging).
4. Background worker consumes events and writes to analytics store/repository.
5. Aggregation queries read from analytics repository (pre-aggregated or on-demand group-by based on backend capability).

### 3.1 Components
- **Redirect instrumentation hook**: lightweight capture + enqueue.
- **Event queue**: bounded, lock-efficient queue abstraction.
- **Analytics worker(s)**: asynchronous consumer(s) with batch write support.
- **Analytics repository**: persistence and aggregate query APIs.
- **Stats endpoint handler**: reads aggregate metrics per slug/link and time range.
- **Retention scheduler hook**: configurable cleanup for stale raw events/rollups.

---

## 4. Event Data Model

`ClickEvent` minimum schema:
- `event_id` (UUID or monotonic unique token)
- `occurred_at` (UTC timestamp)
- `slug` (short code)
- `link_id` (internal identifier if available)
- `domain` (host used for redirect)
- `status_code` (HTTP status returned, e.g., 301/302/404/410)
- `referrer` (nullable string, aggressively truncated/sanitized)
- `user_agent` (nullable string, aggressively truncated)
- `client_id_hash` (anonymized stable hash, never raw IP)


### 4.2 Event size and sanitization limits

To preserve bounded memory and queue behavior, the implementation MUST enforce explicit caps. Recommended defaults:

- `slug`: validated by core link constraints only
- `domain`: normalized host, max 255 bytes
- `referrer`: max 512 bytes after sanitization
- `user_agent`: max 512 bytes after sanitization
- serialized event payload: fixed upper bound (for example 2-4 KiB)
- no wholesale copying of request headers into the event model

Overflow/truncation behavior MUST be deterministic and covered by tests.

### 4.1 Client identifier anonymization
Recommended derivation:
- input: normalized client network identifier (e.g., IP) + configurable server salt/pepper
- algorithm: HMAC-SHA256 (or equivalent keyed hash)
- storage: hex/base64 digest, optionally truncated for size

Requirements:
- raw IP MUST NOT be persisted in analytics event storage by default
- salt/pepper MUST be rotatable via configuration
- documentation MUST describe impact of rotation on longitudinal uniqueness

---

## 5. Redirect Path and Non-Blocking Requirements

## 5.1 Redirect flow integration
On every redirect attempt (success or terminal failure status), handler should:
1. Gather context fields from request/response metadata.
2. Build `ClickEvent` with minimal allocations where feasible.
3. Attempt `TryEnqueue(event)`.
4. Continue response path regardless of enqueue outcome.

The enqueue path MUST remain a tiny, bounded-cost operation suitable for the protected redirect fast path.

## 5.2 Queue behavior
- Queue MUST be bounded by configuration (`analytics.queue_capacity`).
- Enqueue MUST be non-blocking for redirect thread.
- On full queue:
  - increment `analytics_events_dropped_total`
  - optionally emit rate-limited warning logs
  - do not propagate failure to redirect response

## 5.3 Worker behavior
- Worker batch size and flush interval configurable.
- Transient write failure SHOULD trigger bounded retry/backoff.
- Persistent failure SHOULD fail closed for analytics only (drop/spool policy configurable), never for redirect serving.

---

## 6. Storage and Aggregation

## 6.1 Repository interfaces
Define analytics-focused interfaces:

```cpp
class IClickEventRepository {
public:
  virtual ~IClickEventRepository() = default;
  virtual Result<void, AnalyticsError> InsertBatch(const std::vector<ClickEvent>& events) = 0;
  virtual Result<AggregateStats, AnalyticsError> GetAggregateStats(const AggregateQuery& query) = 0;
  virtual Result<uint64_t, AnalyticsError> DeleteOlderThan(Timestamp cutoff) = 0;
};

class IAnalyticsSink {
public:
  virtual ~IAnalyticsSink() = default;
  virtual Result<void, AnalyticsError> Emit(const ClickEvent& event) = 0; // optional future adapter
  virtual Result<void, AnalyticsError> Flush() = 0;
};
```

`IAnalyticsSink` is optional for future external forwarding; internal repository remains source of truth for Stage 04.

## 6.2 Aggregate stats endpoint
Add endpoint for per-link analytics, example:
- `GET /api/v1/links/{slug}/stats?from=<ts>&to=<ts>&bucket=day`

Response minimum:
- `slug`/`link_id`
- `total_attempts`
- `successful_redirects`
- `attempts_by_status_code`
- `attempts_by_domain`
- `time_buckets` (bucket timestamp + count)
- optional metadata (`from`, `to`, `bucket`)

Validation:
- invalid range/bucket -> `400`
- unknown link -> `404` (or `200` with zero counts if product decision dictates; choose one and keep consistent)

---

## 7. Retention and Cleanup Hooks

Retention support:
- `analytics.retention_days` configurable (0/negative disables deletion)
- scheduled cleanup task (timer/cron hook) invokes repository `DeleteOlderThan(now - retention)`
- cleanup MUST run outside redirect request path
- cleanup outcome exposed via metrics/logs

If backend supports rollups:
- retain aggregated rollups longer than raw events
- ensure cleanup order does not corrupt aggregate endpoint correctness

---

## 8. Observability and Reliability

Required metrics:
- `analytics_events_enqueued_total`
- `analytics_events_dropped_total`
- `analytics_events_persisted_total`
- `analytics_worker_failures_total`
- `analytics_queue_depth` (gauge)
- `analytics_enqueue_latency_us` (optional)

Logging:
- structured warning on queue overflow (rate-limited)
- structured error on write batch failure
- include correlation/request IDs where available

SLO-aligned expectations:
- redirect latency regression due to analytics instrumentation should be negligible (microseconds-scale enqueue)
- analytics degradation must be isolated from redirect availability

---

## 9. Testing Strategy

## 9.1 Unit tests
- `ClickEvent` builder populates required fields and sanitizes/truncates optional strings.
- client anonymization is deterministic for same input+salt and changes with different salt.
- queue `TryEnqueue` is non-blocking and drops on capacity overflow.
- worker retry logic honors retry bounds and failure isolation.

## 9.2 Integration tests
- redirect request emits expected event attributes and status codes.
- redirect success path remains successful when analytics repository fails.
- aggregate endpoint returns correct totals and buckets for fixture events.
- retention hook deletes events older than configured threshold.

## 9.3 Performance checks
- benchmark redirect endpoint with analytics enabled vs disabled.
- verify no unacceptable tail-latency regression under queue pressure.
- run sustained overload tests with a saturated queue to verify bounded memory, predictable drop metrics, and redirect availability preservation.

---

## 10. Privacy and Data Handling Assumptions

1. Raw client IP is ephemeral in request context and not persisted in event store by default.
2. `client_id_hash` is pseudonymous and may still be personal data in some jurisdictions; treat accordingly.
3. `referrer` and `user_agent` may contain sensitive data; enforce length limits and optional redaction policy.
4. Retention defaults should minimize stored analytics lifetime while preserving operational usefulness.
5. Documentation must clearly state anonymization method and rotation trade-offs.

---

## 11. Implementation Plan (Suggested Order)

1. Define `ClickEvent` model + anonymization helper.
2. Add queue abstraction and non-blocking enqueue in redirect handler.
3. Implement worker and analytics repository batch insert path.
4. Add aggregate stats query model and endpoint.
5. Add retention scheduler hook + config.
6. Add metrics/logging instrumentation.
7. Implement unit/integration/performance tests.
8. Update docs/README with privacy assumptions and operational knobs.

---

## 12. Exit Criteria Mapping

- **Redirects remain fast**: validated by non-blocking enqueue design and performance checks.
- **Click information recorded correctly**: validated by event model requirements and integration tests.
- **Analytics failures do not break redirects**: enforced by best-effort enqueue/write behavior and failure-isolation tests.
