# Analytics

## Event pipeline overview

Redirect attempts are normalized into click events and enqueued on a bounded in-memory queue. Redirect handling never waits for persistence; a background worker drains batches to the configured repository.

## Availability guarantees

- Redirect hot path is non-blocking with respect to analytics.
- Queue overflow drops events (best-effort) and increments drop counters.
- Worker/repository failures do not change redirect responses.

## Privacy model

- Raw client IP is not persisted by default.
- `client_id_hash` is produced via keyed hash (salted).
- Salt rotation reduces long-lived linkability but breaks hash continuity across rotation boundaries.

## Retention

- `analytics.retention_days <= 0` disables cleanup.
- Positive retention runs cleanup with cutoff `now(UTC) - retention_days`.

## Metrics

- `analytics_events_enqueued_total`
- `analytics_events_dropped_total`
- `analytics_events_persisted_total`
- `analytics_worker_failures_total`
- `analytics_queue_depth`
- `analytics_enqueue_latency_us` (optional)
- `analytics_retention_deleted_total` (optional)
- `analytics_retention_failures_total` (optional)

## Stats endpoint example

`GET /api/v1/links/{slug}/stats?from=1710000000&to=1710086400&bucket=hour`

## Disabled mode

When `analytics.enabled=false`, redirect behavior is unchanged, event recording is a no-op, and worker startup is skipped.

## Non-goals

- Dashboarding
- External warehouse export
- Exactly-once delivery
