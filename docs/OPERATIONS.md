# Operations Runbook

## Analytics lifecycle
- Startup: validate analytics config and salt requirements.
- Runtime: start worker only when analytics is enabled.
- Shutdown: stop worker gracefully and flush in-flight queue according to retry policy.

## Retention cleanup
Run retention cleanup on an out-of-band schedule (cron/timer) so redirect request path is never blocked.

## Troubleshooting
- High `analytics_events_dropped_total`: increase queue capacity or persistence throughput.
- Rising `analytics_worker_failures_total`: inspect backend connectivity and retry backoff.
- Unexpected empty stats: verify analytics enabled mode and backend wiring.

## Safe defaults
Use bounded queue, conservative batch/retry settings, and finite retention.
