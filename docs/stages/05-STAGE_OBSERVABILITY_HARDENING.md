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
- `component` (http_server, tls, storage, routing, config, etc.)
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


## 4.5 Detailed subtask — Configurable logging engine

### 4.5.1 Objective

Introduce a project-wide configurable logging engine based on a lightweight C++ logging library, and replace all direct writes to `stdout`/`stderr` with structured log entries.

The implementation MUST make logging the only supported path for operational output. Existing usages of `std::cout`, `std::cerr`, `std::clog`, `printf`, `fprintf(stdout, ...)`, `fprintf(stderr, ...)`, `puts`, `perror`, and equivalent ad-hoc output helpers MUST be removed from production code or wrapped behind the logging abstraction.

Recommended library: `spdlog` with `fmt` formatting support.

Acceptable alternative: another lightweight C++ logging library may be used only if it provides level filtering, structured formatting, multiple sinks, low overhead, and straightforward CMake integration.

### 4.5.2 Scope

This subtask covers:

- dependency integration for the chosen logging library
- centralized logger wrapper and configuration model
- component-specific logger creation
- structured log field helpers
- stdout/stderr/file sink configuration
- replacement of all direct console output in production code
- INFO/WARN/ERROR instrumentation across generic runtime events and failure paths
- unit and integration tests for logging behavior
- documentation of all logging configuration keys

This subtask does not cover:

- external log shipping agents
- OpenTelemetry tracing backend integration
- vendor-specific cloud logging configuration
- semantic redesign of metrics or request ID handling beyond log field propagation

### 4.5.3 Configuration keys

Add logging configuration with the following keys:

| Key | Type | Default | Description |
|---|---:|---|---|
| `log.enabled` | bool | `true` | Enables/disables runtime log emission. Fatal startup validation errors may still be printed through emergency fallback before logger initialization. |
| `log.level` | enum | `info` | Minimum emitted level: `trace`, `debug`, `info`, `warn`, `error`, `fatal`, `off`. |
| `log.format` | enum | `key_value` | Output format: `key_value`, `json`, or `plain` for local development. Production default should remain structured. |
| `log.sink` | enum/list | `stderr` | Supported values: `stdout`, `stderr`, `file`, `syslog` where available. Multiple sinks may be supported if simple. |
| `log.file.path` | string | empty | Required only when `log.sink` contains `file`. |
| `log.file.max_size_mb` | integer | `100` | Max file size before rotation when file sink is enabled. |
| `log.file.max_files` | integer | `5` | Number of rotated log files to keep. |
| `log.flush.level` | enum | `error` | Flush immediately on this level or higher. |
| `log.async` | bool | `false` | Optional async logging mode. Keep disabled unless queue behavior is tested. |
| `log.async.queue_size` | integer | `8192` | Queue size for async mode. |
| `log.redact_pii` | bool | `true` | Enables stricter sanitization for remote address, user agent, and user-controlled fields. |
| `log.max_field_length` | integer | `512` | Truncates long user-controlled fields. |

Configuration MUST be loadable from the same configuration sources used by the rest of the service. Invalid logging configuration MUST produce a clear startup error and a `FATAL` log entry if the logger has already been initialized.

### 4.5.4 Required files and classes

Create or update the following implementation files:

| File | Responsibility |
|---|---|
| `include/url_shortener/observability/LogLevel.h` | Defines internal log level enum and conversion helpers. |
| `include/url_shortener/observability/LoggingConfig.h` | Defines validated logging configuration structure. |
| `include/url_shortener/observability/Logger.h` | Public logging facade used by production code. |
| `include/url_shortener/observability/LogFields.h` | Helpers for common structured fields and safe field truncation/redaction. |
| `include/url_shortener/observability/LoggerFactory.h` | Creates component loggers from global logging configuration. |
| `src/observability/LogLevel.cpp` | Parses/serializes log levels. |
| `src/observability/LoggingConfig.cpp` | Validates config values and applies defaults. |
| `src/observability/Logger.cpp` | Implements project logging facade over the selected lightweight library. |
| `src/observability/LogFields.cpp` | Implements structured field helpers, redaction, and truncation. |
| `src/observability/LoggerFactory.cpp` | Initializes sinks, formatter, flush policy, and component logger instances. |
| `src/observability/ConsoleOutputAudit.cpp` | Optional small helper/test-only target for detecting forbidden console output patterns if this fits the build layout. |
| `docs/configuration.md` | Documents logging keys, defaults, examples, and operational notes. |
| `docs/deployment.md` | Explains stdout/stderr structured logging behavior under systemd/container runtimes. |

Suggested classes/interfaces:

```cpp
namespace url_shortener::observability {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
    Off
};

struct LoggingConfig {
    bool enabled = true;
    LogLevel level = LogLevel::Info;
    LogFormat format = LogFormat::KeyValue;
    std::vector<LogSink> sinks = {LogSink::Stderr};
    std::optional<std::filesystem::path> file_path;
    std::size_t file_max_size_bytes = 100 * 1024 * 1024;
    std::size_t file_max_files = 5;
    LogLevel flush_level = LogLevel::Error;
    bool async = false;
    std::size_t async_queue_size = 8192;
    bool redact_pii = true;
    std::size_t max_field_length = 512;
};

struct LogField {
    std::string_view key;
    std::string value;
};

class Logger {
public:
    void trace(std::string_view message, std::initializer_list<LogField> fields = {});
    void debug(std::string_view message, std::initializer_list<LogField> fields = {});
    void info(std::string_view message, std::initializer_list<LogField> fields = {});
    void warn(std::string_view message, std::initializer_list<LogField> fields = {});
    void error(std::string_view message, std::initializer_list<LogField> fields = {});
    void fatal(std::string_view message, std::initializer_list<LogField> fields = {});
};

class LoggerFactory {
public:
    static void initialize(const LoggingConfig& config);
    static Logger component(std::string_view component_name);
    static void shutdown();
};

} // namespace url_shortener::observability
```

Exact names may be adjusted to the repository naming convention, but the implementation MUST preserve the architectural boundary: production code depends on the project facade, not directly on `spdlog` or another third-party logger.

### 4.5.5 Output replacement rules

Production code MUST NOT write directly to process output streams.

Replace patterns as follows:

| Existing pattern | Replacement |
|---|---|
| `std::cout << "server started"` | `logger.info("server_started", {...})` |
| `std::cerr << "failed to bind"` | `logger.error("listener_bind_failed", {...})` |
| `printf(...)` | appropriate `logger.info/debug/warn/error(...)` call |
| `fprintf(stderr, ...)` | appropriate `logger.warn/error/fatal(...)` call |
| `perror("...")` | `logger.error("operation_failed", {errno/code/message fields})` |
| exception catch block printing to console | structured `ERROR` log with exception type/message and component |

Allowed exceptions:

- dedicated CLI tools may print intentional user-facing command output, but only if they are not part of server runtime paths
- unit tests may write diagnostic output through the test framework
- third-party code under dependency/vendor directories is excluded
- emergency startup fallback may write a single minimal message to `stderr` only when logger initialization itself fails before any logger exists

Add a CI/test guard that scans production source files and fails on forbidden console-output patterns outside the explicit allowlist.

### 4.5.6 Required log events

Add `INFO` logs for normal generic events:

- configuration loaded successfully, with source and selected non-secret options
- logger initialized, with level, format, and sink names
- application startup begins/completes
- selected storage backend and successful backend initialization
- HTTP listener bind attempt and successful bind address/port
- TLS enabled/disabled decision and successful certificate load/reload
- graceful shutdown requested and completed
- request completed, via access log entry described in section 4.4
- background worker started/stopped, if Stage 04 analytics/background processing exists

Add `WARN` logs for recoverable or suspicious situations:

- malformed client request or validation failure
- invalid inbound request ID regenerated by server
- oversized user-controlled field truncated
- optional config value invalid and defaulted, if defaulting is allowed
- retryable backend timeout or slow operation above configured threshold
- queue/backpressure condition that does not yet fail the process
- TLS reload failed but previous certificate remains active

Add `ERROR` logs for failed operations requiring operator attention:

- backend connection failure or repository operation failure
- unhandled exception converted to `500`
- TLS handshake failure where reason is available
- request handling failure caused by internal error
- metrics/exporter failure if it prevents observability output but not request serving
- file logger sink initialization/rotation failure after startup

Add `FATAL` logs immediately before termination for unrecoverable cases:

- invalid required configuration
- failure to initialize all configured log sinks
- failure to bind required listener
- missing/unreadable TLS key/certificate when TLS is required
- storage backend unavailable during mandatory startup readiness check

### 4.5.7 Structured field requirements

Each log event MUST include:

- `ts`
- `level`
- `component`
- `msg` or `event`

When available, also include:

- `request_id`
- `operation_id`
- `route`
- `method`
- `path`
- `status`
- `latency_ms`
- `backend`
- `error_code`
- `exception_type`
- `errno` or platform error code

Do not log full request/response bodies by default. Payload snippets are allowed only at `DEBUG` level, must be truncated, and must pass redaction.

### 4.5.8 Dependency and CMake requirements

- Integrate the selected lightweight logging library through the repository's existing dependency strategy.
- Prefer system/package dependency if the project already uses packaged dependencies; otherwise use CMake `FetchContent` or vendored dependency lock file consistently with repository policy.
- Production source files MUST include only project logging headers, not third-party logger headers.
- Add a CMake option if useful: `URL_SHORTENER_ENABLE_ASYNC_LOGGING` or equivalent.
- Ensure logging builds on all supported platforms and compilers.

### 4.5.9 Tests

Add unit tests:

- `LoggingConfigTest.UsesDefaultsWhenConfigOmitted`
- `LoggingConfigTest.RejectsInvalidLevel`
- `LoggingConfigTest.RejectsInvalidSinkCombination`
- `LoggerTest.FiltersBelowConfiguredLevel`
- `LoggerTest.EmitsInfoWarnErrorFatalLevels`
- `LoggerTest.InjectsComponentAndCommonFields`
- `LoggerTest.TruncatesLongUserControlledFields`
- `LoggerTest.RedactsSensitiveFields`
- `LoggerFactoryTest.CreatesComponentLogger`
- `ConsoleOutputAuditTest.NoForbiddenConsoleOutputInProductionSources`

Add integration tests:

- `LoggingIntegration.StartupEmitsInfoEvents`
- `LoggingIntegration.RequestCompletionProducesAccessLog`
- `LoggingIntegration.ValidRequestIdAppearsInRequestLogs`
- `LoggingIntegration.MalformedRequestProducesWarning`
- `LoggingIntegration.BackendFailureProducesError`
- `LoggingIntegration.FatalStartupFailureIsLoggedBeforeExit`
- `LoggingIntegration.StdoutStderrOutputIsStructuredLogOnly`

Integration tests SHOULD capture process output and assert that emitted lines are valid configured log format entries, not ad-hoc text.

### 4.5.10 Migration checklist

Implementation agent MUST audit and update:

- server startup/shutdown code
- configuration loader
- HTTP request parser/dispatcher
- route handlers
- redirect/create/statistics operations
- storage/repository backends
- TLS initialization/reload paths
- background workers/analytics queues if present
- CLI/service entry points
- exception handling boundaries
- tests that previously expected text on stdout/stderr

### 4.5.11 Acceptance criteria

This subtask is complete only when:

1. The service has one centralized logging facade and no production module logs directly through third-party APIs.
2. Runtime logging behavior is configurable through documented config keys.
3. All server-runtime writes to `stdout` and `stderr` happen through the logging engine.
4. Production source code contains no unapproved `std::cout`, `std::cerr`, `std::clog`, `printf`, `fprintf`, `puts`, or `perror` usage.
5. Generic lifecycle events produce `INFO` logs.
6. Recoverable/suspicious situations produce `WARN` logs.
7. Internal failures and operator-actionable failures produce `ERROR` logs.
8. Unrecoverable startup/runtime failures produce `FATAL` logs before termination.
9. Request completion logs include request correlation fields and match section 4.4.
10. Logging unit and integration tests pass in CI.
11. Documentation includes logging configuration, examples, and operational guidance for systemd/container log collection.

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
2. Complete Subtask 5.1: implement configurable logging engine, integrate lightweight logging library, replace direct stdout/stderr output with structured log entries, and add INFO/WARN/ERROR/FATAL instrumentation.
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
