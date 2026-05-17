# Configuration Reference

This document lists runtime configuration keys for Stage 05 operations.

## Core server

| Key | Type | Default | Valid values | Required | Reloadable | Notes |
|---|---|---:|---|---|---|---|
| `host` | string | `0.0.0.0` | valid bind address | no | no | Listener bind host. |
| `port` | uint16 | `8080` | `1..65535` | no | no | Public HTTP port. |
| `threads` | uint32 | `0` (auto) | `0..1024` | no | no | Worker thread count. |
| `request_id.max_length` | uint32 | `64` | `1..256` | no | yes | Max accepted inbound request ID header length. |

## Logging

| Key | Type | Default | Valid values | Required | Reloadable | Notes |
|---|---|---:|---|---|---|---|
| `log.level` | enum | `INFO` | `TRACE,DEBUG,INFO,WARN,ERROR,FATAL` | no | yes | Minimum log level. |
| `log.format` | enum | `json` | `json,key_value` | no | yes | Structured log output encoding. |
| `log.sink` | enum | `stdout` | `stdout,file,syslog` | no | no | Sink target for structured logs. |
| `log.file` | string | empty | filesystem path | no | no | Required when `log.sink=file`. |
| `log.redact_pii` | bool | `true` | `true/false` | no | yes | Enables stricter redaction/truncation behavior. |

## HTTP hardening

| Key | Type | Default | Valid values | Required | Reloadable | Notes |
|---|---|---:|---|---|---|---|
| `http.max_header_bytes` | uint32 | `16384` | `1024..1048576` | no | no | Upper bound on total header bytes. |
| `http.max_body_bytes` | uint32 | `1048576` | `1..134217728` | no | yes | Request body limit with early reject. |
| `http.max_path_bytes` | uint32 | `4096` | `1..65535` | no | no | Path/query hard limit for malformed-path prevention. |
| `http.request_timeout_ms` | uint32 | `30000` | `100..600000` | no | yes | Request timeout for slow clients/backends. |

## Metrics and probes

| Key | Type | Default | Valid values | Required | Reloadable | Notes |
|---|---|---:|---|---|---|---|
| `metrics.enabled` | bool | `true` | `true/false` | no | yes | Enables in-process counters/histograms. |
| `metrics.path` | string | `/metrics` | absolute path | no | no | Pull endpoint path. |
| `metrics.route_label_mode` | enum | `template` | `template` | no | no | Prevents high-cardinality path labels. |
| `health.path` | string | `/healthz` | absolute path | no | no | Liveness endpoint path. |
| `readiness.path` | string | `/readyz` | absolute path | no | no | Readiness endpoint path. |

## TLS

| Key | Type | Default | Valid values | Required | Reloadable | Notes |
|---|---|---:|---|---|---|---|
| `tls.enabled` | bool | `false` | `true/false` | no | no | Enable TLS listener. |
| `tls.cert_file` | string | empty | readable PEM path | if enabled | yes (reload) | Never log full PEM body. |
| `tls.key_file` | string | empty | readable key path | if enabled | yes (reload) | Permissions must restrict access. |
| `tls.key_passphrase_env` | string | empty | env var name | no | no | Optional passphrase source; redact in logs. |
| `tls.reload_signal` | string | `SIGHUP` | `SIGHUP` | no | no | Trigger cert/key rotation without restart. |

## Storage/backend (summary)

See [`docs/storage/configuration.md`](./storage/configuration.md) for backend-specific keys.

## Security/operational notes

- Never persist or emit secrets in logs.
- Keep `log.level=INFO` in production; temporarily elevate to `DEBUG` only during incident response.
- Keep request ID limits conservative to prevent header abuse.
- Ensure metrics/probe endpoints are protected by network policy when required.
