#pragma once
#include <string>
#include <vector>

#include "url_shortener/observability/logging_config.hpp"

namespace url_shortener::observability
{

/**
 * @brief A single structured log field: a key/value string pair
 * appended to a log message.
 *
 * Values are sanitized by `SanitizeField` before emission; fields
 * with key `"remote_addr"` or `"user_agent"` are treated as PII and
 * redacted when `LoggingConfig::redact_pii` is set.
 */
struct LogField
{
    ///< Field name used as the structured key in log output.
    std::string key;

    ///< Raw field value; may be clamped or redacted before output.
    std::string value;
};

/**
 * @brief Lightweight structured logger that emits to the sink
 * configured via `LoggerFactory::Initialize`.
 *
 * Each instance carries a `component` label that is included on
 * every log line to identify the subsystem. Instances are cheaply
 * copyable and may be held by value.
 *
 * Thread-safety: individual logging calls (`trace`, `debug`, etc.)
 * are safe to invoke concurrently. The underlying write to
 * `std::cout` or `std::cerr` is serialized by the stream's own
 * internal locking in C++11 and later.
 */
class Logger
{
public:
    /**
     * @brief Constructs a logger with the default component label
     * `"app"`.
     */
    Logger() = default;

    /**
     * @brief Constructs a logger with the given component label.
     * @param component Subsystem label emitted on every log line
     * produced by this instance.
     */
    explicit Logger(std::string component)
        : component_(std::move(component))
    {
    }

    /**
     * @brief Emits a message at `Trace` severity.
     * @param msg Human-readable log message.
     * @param f Optional structured fields appended to the line.
     */
    void trace(const std::string& msg, std::vector<LogField> f = {}) const;

    /**
     * @brief Emits a message at `Debug` severity.
     * @param msg Human-readable log message.
     * @param f Optional structured fields appended to the line.
     */
    void debug(const std::string& msg, std::vector<LogField> f = {}) const;

    /**
     * @brief Emits a message at `Info` severity.
     * @param msg Human-readable log message.
     * @param f Optional structured fields appended to the line.
     */
    void info(const std::string& msg, std::vector<LogField> f = {}) const;

    /**
     * @brief Emits a message at `Warn` severity.
     * @param msg Human-readable log message.
     * @param f Optional structured fields appended to the line.
     */
    void warn(const std::string& msg, std::vector<LogField> f = {}) const;

    /**
     * @brief Emits a message at `Error` severity.
     * @param msg Human-readable log message.
     * @param f Optional structured fields appended to the line.
     */
    void error(const std::string& msg, std::vector<LogField> f = {}) const;

    /**
     * @brief Emits a message at `Fatal` severity.
     * @param msg Human-readable log message.
     * @param f Optional structured fields appended to the line.
     */
    void fatal(const std::string& msg, std::vector<LogField> f = {}) const;

private:
    ///< Subsystem label emitted on every log line; defaults to
    ///< `"app"` when constructed without an explicit name.
    std::string component_ = "app";

    /**
     * @brief Core emit implementation: formats and writes one log
     * line if `lvl` is enabled by the global configuration.
     *
     * Line format: `ts=<ISO8601Z> level=<LEVEL>
     * component=<component_> msg=<msg> [field=value ...]`.
     * @param lvl Severity for this message.
     * @param msg Human-readable message text.
     * @param f Structured fields to append after the message.
     */
    void log(LogLevel lvl,
             const std::string& msg,
             std::vector<LogField> f) const;
};

}  // namespace url_shortener::observability
