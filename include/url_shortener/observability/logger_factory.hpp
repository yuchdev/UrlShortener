#pragma once
#include "url_shortener/observability/logger.hpp"

namespace url_shortener::observability
{

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
     * with an empty `file_path`, or `max_field_length == 0`).
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
};

}  // namespace url_shortener::observability
