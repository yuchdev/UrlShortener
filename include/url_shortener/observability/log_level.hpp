#pragma once
#include <optional>
#include <string>

namespace url_shortener::observability
{

/**
 * @brief Severity levels for the logging subsystem, ordered from
 * least to most severe.
 *
 * Values are compared ordinally: a logger configured at level `L`
 * suppresses messages whose level is less severe than `L`.
 */
enum class LogLevel
{
    ///< Highly detailed diagnostic traces; suppressed in most
    ///< environments by default.
    Trace,

    ///< Developer-facing diagnostic messages.
    Debug,

    ///< Normal operational messages about system state.
    Info,

    ///< Unexpected conditions the system can recover from.
    Warn,

    ///< Errors that prevented an operation from completing.
    Error,

    ///< Critical errors; typically triggers a controlled shutdown.
    Fatal,

    ///< Logging disabled; no messages are emitted at any severity.
    Off
};

/**
 * @brief Converts a log level to its uppercase string representation.
 *
 * Maps each level to a fixed uppercase name: `"TRACE"`, `"DEBUG"`,
 * `"INFO"`, `"WARN"`, `"ERROR"`, `"FATAL"`, `"OFF"`. An unrecognised
 * value returns `"INFO"` as a safe fallback.
 * @param level Level to convert.
 * @return Uppercase ASCII name of `level`.
 */
std::string ToString(LogLevel level);

/**
 * @brief Parses a log level from a case-insensitive string.
 *
 * Accepts `"trace"`, `"debug"`, `"info"`, `"warn"`, `"warning"`,
 * `"error"`, `"fatal"`, and `"off"` (all case-insensitive; both
 * `"warn"` and `"warning"` map to `LogLevel::Warn`).
 * @param value String to parse.
 * @return The matching `LogLevel` if `value` is recognised;
 * `std::nullopt` otherwise.
 */
std::optional<LogLevel> ParseLogLevel(const std::string& value);

}  // namespace url_shortener::observability
