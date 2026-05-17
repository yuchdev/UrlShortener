# Stage 05 Release and Migration Notes

## Notable observability changes

- Structured multi-level logging expectations are formalized.
- Request/correlation ID propagation and error-envelope presence are required.
- Metrics inventory includes traffic, malformed input, TLS, backend, and runtime signals.
- Health/readiness probe usage is standardized.

## New/updated configuration keys

- `log.level`, `log.format`, `log.sink`, `log.redact_pii`
- `request_id.max_length`
- `http.max_header_bytes`, `http.max_body_bytes`, `http.max_path_bytes`, `http.request_timeout_ms`
- `metrics.enabled`, `metrics.path`
- `health.path`, `readiness.path`
- TLS reload-related keys

## Backward compatibility

- API response envelope remains Stage 01-compatible with added `request_id` in errors.
- Existing endpoints remain unchanged; observability additions are additive.
- Metrics label normalization avoids raw-path cardinality explosions.

## Operator upgrade checklist

1. Review and set production log level (`INFO` default).
2. Configure request ID header limits and sanitization policy.
3. Enable/validate `/metrics`, `/healthz`, `/readyz` in monitoring and load balancer checks.
4. Validate malformed-input handling against security test cases.
5. Rehearse TLS reload and rollback runbook in staging.
6. Capture baseline benchmark results before and after rollout.
