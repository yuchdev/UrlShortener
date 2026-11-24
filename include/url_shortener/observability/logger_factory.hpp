#pragma once
#include "url_shortener/observability/logger.hpp"

namespace url_shortener::observability
{

class FileSink;

/**
 * @brief Process-wide singleton factory that owns the active logging
 * configuration and dispenses pre-configured `Logger` instances.
 *
 * Call `Initialize` once at startup before creating any `Logger`.
 * `Component` and `Config` return values derived from the config set
 * by the most recent successful `Initialize` call; they return a
 * default-constructed config if `Initialize` has not been called.
 *
 * Thread-safety: `Initialize` must complete before any concurrent
 * calls to `Component` or `Config`. After initialization, `Component`
 * and `Config` are safe to call concurrently.
 */
class LoggerFactory
{
public:
    /**
     * @brief Validates `cfg` and stores it as the active logging
     * configuration.
     *
     * Calling `Initialize` more than once replaces the previous
     * configuration.
     * @param cfg Configuration to validate and activate; see
     * `ValidateLoggingConfig` for the validation rules.
     * @param error If non-null and validation fails, receives a
     * human-readable description of the failure; left untouched
     * on success.
     * @return `true` if `cfg` is valid and was stored; `false` if
     * `ValidateLoggingConfig` rejected `cfg` (e.g. `LogSink::File`
     * with an empty `file_path`, or `max_field_length == 0`), or if
     * `cfg.sink == LogSink::File` and the file could not be opened
     * (missing directory, permission denied, or a symlink at the final
     * path component). On a `File`-open failure `error` receives a
     * fixed, value-free diagnostic that never contains the path, and the
     * previously active configuration and file sink are left untouched
     * (fail-closed: output is never silently redirected to `stderr`).
     */
    static bool Initialize(const LoggingConfig& cfg,
                           std::string* error = nullptr);

    /**
     * @brief Creates a `Logger` instance bound to `component`.
     * @param component Subsystem label that appears on every line
     * emitted by the returned logger.
     * @return A new `Logger` instance; lightweight and safe to
     * copy or store by value.
     */
    static Logger Component(const std::string& component);

    /**
     * @brief Returns the active logging configuration.
     *
     * Returns a default-constructed `LoggingConfig` if `Initialize`
     * has not been called yet.
     * @return Reference to the process-wide logging configuration;
     * valid for the lifetime of the process and may change if
     * `Initialize` is called again.
     */
    static const LoggingConfig& Config();

    /**
     * @brief Returns the open file sink when `Config().sink == LogSink::File`.
     *
     * Non-owning observer of the sink opened by the most recent successful
     * @ref Initialize call. Used by `Logger::log` to route a line to the file
     * destination. Because @ref Initialize fails closed, a successful
     * `LogSink::File` initialization always yields a non-null sink here.
     *
     * @return The active @ref FileSink, or `nullptr` when the current sink is
     * not `LogSink::File` (or `Initialize` has not been called). The pointer is
     * owned by the factory and is valid until the next @ref Initialize call.
     */
    static FileSink* ActiveFileSink();
};

}  // namespace url_shortener::observability
