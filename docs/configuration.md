# Configuration Reference (Stage 05 Scope 1)

## Observability and hardening keys

| Key | CLI flag | Type | Default | Notes |
|---|---|---|---|---|
| request_id.max_length | `--request-id-max-length` | uint32 | `64` | Maximum inbound `X-Request-Id` length. Invalid IDs are ignored and replaced. |
| request.max_body_bytes | `--max-request-body-bytes` | uint32 | `65536` | Request body hard cap. Exceeding requests return `413 payload_too_large`. |
| request.max_target_length | `--max-request-target-length` | uint32 | `2048` | Request target/path hard cap. Exceeding requests return `414 target_too_large`. |

## Endpoints added in this scope

- `GET /metrics` — text metrics for in-process counters (`http_requests_total`, inflight, parse/malformed, TLS reload counters).
- `GET /healthz` — liveness probe (`200 ok`).
- `GET /readyz` — readiness probe (`200 ok`).

## Request ID behavior

- Accepts inbound `X-Request-Id` when valid (`[A-Za-z0-9-_.]`, bounded by configured max length).
- Otherwise generates a server-side 128-bit hex request ID.
- Response always echoes `X-Request-Id`.
- Error envelope always includes `error.request_id`.
