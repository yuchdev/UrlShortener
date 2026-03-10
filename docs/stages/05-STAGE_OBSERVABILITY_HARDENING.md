# Stage 05 Technical Specification

## Add Observability, Documentation, and Hardening

## 1. Purpose and Scope

This specification defines Stage 05 work required to move the service from feature-complete to production-operable.

The stage delivers:
- multilevel, structured logging with operationally meaningful fields
- request/correlation ID propagation across request lifecycle and async boundaries
- metrics hooks/internal counters for traffic, failures, and system health
- complete operator/developer documentation for configuration, deployment, backend selection, and TLS operations
- malformed-input hardening tests and boundary-condition review
- load/performance benchmark plan and baseline reporting format
- release and migration notes for operators adopting Stage 05

Out of scope:
- vendor-specific observability stack provisioning (e.g., Grafana/Prometheus deployment)
- distributed tracing backend integration requiring external infrastructure
- major protocol redesign of existing APIs

---

## 2. Goals and Non-Goals

### 2.1 Goals
1. Operators MUST be able to diagnose request failures and performance regressions from logs + metrics without code changes.
2. Every inbound request MUST have a stable request identifier visible in logs and error responses. The response schema should extend the Stage 01 error envelope rather than invent a brand-new late-stage format.
3. Critical runtime counters MUST be available in-process and/or through an export hook.
4. Project documentation MUST be sufficient for a new engineer to configure, deploy, and run production operations.
5. Input parsing and error-path behavior MUST be hardened against malformed/oversized/unexpected payloads.

### 2.2 Non-Goals
1. Full OpenTelemetry adoption with external collector topology.
2. Auto-scaling orchestration recipes for all cloud providers.
3. Replacing existing core storage/network components.

---

## 3. Operational Readiness Principles

1. **Diagnosable by default**: all important state transitions should emit structured logs at proper levels.
2. **Low-overhead observability**: instrumentation must not materially degrade hot-path latency.
3. **Fail-safe behavior**: observability failures (e.g., metric exporter unavailable) must not break request serving.
4. **Deterministic error contracts**: malformed inputs should produce stable, documented error responses.
5. **Runbook-first docs**: deployment and TLS rotation procedures should be executable by a non-author engineer.

---

## 4. Logging Architecture

## 4.1 Log model
Implement a centralized logger abstraction with the following characteristics:
- levels: `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL`
- structured fields support (`key=value` or JSON mode)
- sink abstraction (stdout/file/system logger)
- runtime-configurable minimum level (`log.level`)

Required common fields:
- `ts` (UTC ISO-8601 timestamp)
- `level`
- `msg`
- `request_id` (if request-scoped)
- `component` (url_shortener, tls, storage, routing, config, etc.)
- `remote_addr` (when available and policy-allowed)
- `method`, `path`, `status`, `latency_ms` for request completion logs

## 4.2 Logging policy by level
- `TRACE`: very fine-grained diagnostic events (disabled in production)
- `DEBUG`: developer diagnostics, branch decisions, validation details
- `INFO`: startup, shutdown, listener binds, successful cert reload, periodic summaries
- `WARN`: recoverable failures, malformed requests, queue overflows, fallback behaviors
- `ERROR`: request failures due to internal issues, TLS handshake errors, backend unavailability
- `FATAL`: unrecoverable startup/runtime conditions preceding termination

## 4.3 Sensitive data controls
- never log private keys, passphrases, raw secrets, or full certificate PEM bodies
- redact auth headers/tokens if present
- truncate long user-controlled fields (`path`, `user_agent`, payload snippets)
- provide optional `log.redact_pii=true` mode for stricter sanitization

## 4.4 Access log requirements
Emit one completion log line per request with:
- request_id
- method/path
- normalized route label
- status code
- response size (bytes)
- latency
- backend/storage target identifier (if applicable)

---

## 5. Correlation and Request IDs

## 5.1 Generation and propagation
- Accept inbound `X-Request-Id` (or configurable header) if valid and below max length.
- Otherwise generate server-side ID (UUIDv4 or high-entropy 128-bit hex).
- Attach ID to request context for all handlers and async jobs.
- Echo ID in response header (`X-Request-Id`) and structured error payloads.

## 5.2 Validation rules
- max length configurable (`request_id.max_length`, default 64)
- allowed charset: `[A-Za-z0-9-_.]`
- invalid inbound IDs are ignored and regenerated; emit rate-limited warning

## 5.3 Async boundary requirements
- analytics worker / background tasks must preserve parent request ID where causal linkage exists
- when no parent request exists (timer/background), generate operation ID and log as `request_id`

---

## 6. Metrics and Internal Counters

## 6.1 Metric categories
1. **Traffic**
   - `http_requests_total{method,route,status_class}`
   - `http_inflight_requests`
   - `http_request_duration_ms` (histogram buckets)
2. **Errors and robustness**
   - `http_parse_errors_total{reason}`
   - `http_malformed_requests_total{category}`
   - `internal_errors_total{component,type}`
3. **TLS**
   - `tls_handshake_success_total`
   - `tls_handshake_failure_total{reason}`
   - `tls_reload_success_total`
   - `tls_reload_failure_total`
4. **Backend/storage**
   - `backend_requests_total{backend,operation,result}`
   - `backend_latency_ms{backend,operation}`
5. **Process/runtime**
   - `uptime_seconds`
   - `build_info{version,commit}` (gauge=1)

## 6.2 Export strategy
At minimum, provide one of:
- pull endpoint (e.g., `/metrics`) in text format, or
- internal metrics registry interface with pluggable exporter

Requirements:
- metrics collection must be lock-efficient and thread-safe
- metric update failure should not fail user requests
- cardinality control for labels (route templates rather than raw path)

## 6.3 Health and readiness hooks (recommended)
- `/healthz` liveness: process/thread pool alive
- `/readyz` readiness: listeners active + critical backend connectivity checks

---

## 7. Error Handling and Boundary Condition Review

## 7.1 Error taxonomy
Standardize internal error classes:
- validation/input errors -> client `4xx`
- resource absence/conflict -> `404/409`
- backend transient failure -> `503`
- unexpected internal errors -> `500`

Each error should map to:
- stable machine-readable code
- human-readable message
- request_id

Recommended envelope (compatible with Stage 01):

```json
{
  "error": {
    "code": "invalid_url",
    "message": "url must be an absolute http/https URL",
    "request_id": "req_123"
  }
}
```

## 7.2 Boundary conditions checklist
- empty body, missing content-type, malformed JSON
- oversized headers, path, query, body
- invalid UTF-8 or control chars in input
- unsupported methods and protocol anomalies
- duplicate/conflicting headers
- timeout and cancellation behavior
- backend unavailable/slow response and retry bounds

## 7.3 Hardening expectations
- enforce payload size limits with configurable caps
- early reject malformed requests before expensive processing
- ensure parser exceptions/errors cannot crash process
- apply consistent response shape for all error exits

---

## 8. Test Strategy (Hardening + Observability)

## 8.1 Unit tests
- logger level filtering and field injection
- request ID validation/generation and header echo behavior
- metric counter/histogram registration and update correctness
- error mapping from internal category to API response

## 8.2 Integration tests
- request logs include request_id, route, status, latency
- inbound `X-Request-Id` is preserved when valid
- malformed payload cases return expected status/code/message
- TLS reload success/failure paths update counters and logs
- backend failure paths preserve availability and emit error metrics

## 8.3 Fuzz/negative tests (where practical)
- malformed HTTP lines/headers fuzz corpus
- JSON mutation corpus for parser hardening
- random large/unicode payload combinations

## 8.4 Regression gates
- no crash on malformed inputs in automated suite
- minimum coverage on validation/error modules (target, e.g., >=80%)
- deterministic golden tests for error payload schema

---

## 9. Documentation Deliverables

## 9.1 Configuration reference (`docs/configuration.md`)
Must document every config key with:
- name
- type
- default
- valid range/enumeration
- required/reloadability at runtime
- security/operational notes

## 9.2 Deployment guide (`docs/deployment.md`)
Include:
- build artifact expectations
- environment variables/CLI flags
- recommended service manager configuration (systemd example)
- port/network/firewall requirements
- startup/shutdown and rolling restart procedure
- health-check integration guidance

## 9.3 Backend selection guide (`docs/backend-selection.md`)
Include:
- supported backends and maturity level
- performance/consistency tradeoffs
- failure modes and operational complexity
- migration considerations between backends

## 9.4 TLS setup and rotation runbook (`docs/tls-operations.md`)
Include:
- certificate/key provisioning requirements
- file permissions and ownership expectations
- verification commands (`openssl s_client`, expiry checks)
- zero/low-downtime cert rotation procedure
- SIGHUP reload workflow and validation checklist
- rollback steps for failed rotations

## 9.5 Benchmark notes (`docs/benchmarking.md`)
Include:
- test environment specification (CPU, memory, network, build flags)
- tooling and command examples (`wrk`/`hey`/custom harness)
- baseline metrics template (RPS, p50/p95/p99 latency, error rate, CPU/memory)
- per-route comparisons at minimum for redirect cache hit, redirect cache miss + repository hit, create link, and stats endpoint
- analytics enabled vs disabled comparisons where Stage 04 is present
- comparison methodology (before/after instrumentation)

## 9.6 Release and migration notes (`docs/release-notes/stage-05.md`)
Include:
- notable observability changes
- new config keys and defaults
- backward compatibility notes
- operator action checklist before upgrade

---

## 10. Implementation Plan (Suggested Order)

1. Introduce request context object containing request_id, start time, route label.
2. Add logger abstraction with level filtering and structured fields.
3. Instrument request lifecycle completion logging.
4. Add metrics registry and counters/histograms in request/TLS/backend paths.
5. Normalize error handling and stable error response contract.
6. Add malformed input test suites and negative scenarios.
7. Produce full docs set (config, deployment, backend, TLS, benchmark notes).
8. Add release/migration notes and operational checklist.

---

## 11. Risks and Mitigations

- **Risk:** High-cardinality metrics from raw path labels.  
  **Mitigation:** emit templated route labels only.

- **Risk:** Excessive log volume impacts IO and cost.  
  **Mitigation:** sane default level (`INFO`), sampling/rate-limits for repetitive warnings.

- **Risk:** Correlation IDs can be abused with oversized headers.  
  **Mitigation:** strict validation and max length cap.

- **Risk:** Hardening tests become flaky with timing/network dependence.  
  **Mitigation:** deterministic fixtures; isolate load tests from CI pass/fail gating.

---

## 12. Exit Criteria Mapping

- **System is operationally understandable**: delivered through structured logs, metrics, health/readiness hooks, and deployment/TLS/backend docs.
- **Logs are useful for debugging and production support**: multilevel log policy, required fields, redaction controls, and request correlation IDs.
- **Documentation is sufficient to deploy and extend**: complete config reference, runbooks, backend selection guidance, benchmark process, and release notes.

