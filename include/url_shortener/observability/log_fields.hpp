#pragma once
#include <string>

#include "url_shortener/observability/logging_config.hpp"

namespace url_shortener::observability
{

/**
 * @brief Sanitizes a log field value according to the active
 * logging configuration.
 *
 * If `pii` is `true` and `cfg.redact_pii` is set, the value is
 * replaced with `"[REDACTED]"` regardless of its content. Otherwise,
 * if the value exceeds `cfg.max_field_length` bytes it is truncated
 * to that limit; values within the limit are returned unchanged.
 * @param value Raw field value to sanitize.
 * @param cfg Active logging configuration; `redact_pii` and
 * `max_field_length` are the relevant fields.
 * @param pii `true` if the field carries personally-identifiable
 * information (e.g. remote IP or User-Agent) that must be redacted
 * when `cfg.redact_pii` is set.
 * @return `"[REDACTED]"` if `pii && cfg.redact_pii`; a clamped
 * copy if the value exceeds `cfg.max_field_length`; otherwise
 * `value` unchanged.
 */
std::string SanitizeField(const std::string& value,
                          const LoggingConfig& cfg,
                          bool pii = false);

}  // namespace url_shortener::observability
