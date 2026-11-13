#pragma once
#include <string>

#include "url_shortener/observability/log_level.hpp"

namespace url_shortener::observability
{

/**
 * @brief Output format for structured log lines.
 */
enum class LogFormat
{
    ///< Structured `key=value` pairs on one line per message
    ///< (e.g. `ts=... level=INFO component=... msg=...`).
    KeyValue,

    ///< One JSON object per message.
    Json,

    ///< Unstructured free-text; no structured fields emitted.
    Plain
};

/**
 * @brief Destination sink for log output.
 */
enum class LogSink
{
    ///< Write to standard output (file descriptor 1).
    Stdout,

    ///< Write to standard error (file descriptor 2).
    Stderr,

    ///< Write to a rotating log file; `LoggingConfig::file_path`
    ///< must be non-empty when this sink is selected.
    File
};

/**
 * @brief Runtime configuration for the logging subsystem.
 *
 * Passed to `LoggerFactory::Initialize` before first use. All
 * fields carry meaningful defaults; construct and pass to
 * `Initialize` after overriding only the values you care about.
 */
struct LoggingConfig
{
    ///< When `false`, all log output is suppressed regardless
    ///< of any other setting.
    bool enabled = true;

    ///< Minimum severity to emit; messages below this level are
    ///< discarded silently. `LogLevel::Off` disables all output.
    LogLevel level = LogLevel::Info;

    ///< Serialization format for each log line.
    LogFormat format = LogFormat::KeyValue;

    ///< Destination for log output.
    LogSink sink = LogSink::Stderr;

    ///< File path used when `sink == LogSink::File`; must be
    ///< non-empty in that case (enforced by `ValidateLoggingConfig`).
    std::string file_path {};

    ///< Maximum size of a single log file in megabytes before
    ///< rotation; used only when `sink == LogSink::File`.
    size_t file_max_size_mb = 100;

    ///< Maximum number of rotated log files to retain;
    ///< used only when `sink == LogSink::File`.
    size_t file_max_files = 5;

    ///< Minimum severity that triggers an immediate flush to the
    ///< sink; messages below this level may be buffered.
    LogLevel flush_level = LogLevel::Error;

    ///< When `true`, log writes are queued to a background thread
    ///< (reserved for future use; not wired in the current
    ///< implementation).
    bool async = false;

    ///< Capacity of the async queue in number of messages; used
    ///< only when `async == true`.
    size_t async_queue_size = 8192;

    ///< When `true`, fields whose key is known to carry PII
    ///< (e.g. `remote_addr`, `user_agent`) are replaced with
    ///< `"[REDACTED]"` by `SanitizeField`.
    bool redact_pii = true;

    ///< Maximum byte length of any single field value before
    ///< truncation. Must be > 0 (enforced by
    ///< `ValidateLoggingConfig`).
    size_t max_field_length = 512;
};

/**
 * @brief Validates a `LoggingConfig` for mandatory-field constraints.
 *
 * Currently checks:
 * - If `sink == LogSink::File`, `file_path` must be non-empty.
 * - `max_field_length` must be greater than zero.
 * @param cfg Configuration to validate.
 * @param error Receives a human-readable failure description if
 * validation fails; left unmodified on success.
 * @return `true` if the configuration passes all checks; `false`
 * if `file_path` is empty for a `File` sink, or if
 * `max_field_length == 0`.
 */
bool ValidateLoggingConfig(const LoggingConfig& cfg, std::string& error);

}  // namespace url_shortener::observability
