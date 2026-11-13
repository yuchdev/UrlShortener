#pragma once

#include <map>
#include <string>

namespace observability
{

/**
 * @brief Builds stable metric labels for repository operations.
 *
 * Returns a two-entry map `{"backend": B, "operation": O}` where
 * `B` is normalized to one of `"inmemory"`, `"sqlite"`,
 * `"postgres"`, `"redis"`, `"none"`, `"noop"`, or `"unknown"` for
 * unrecognised values; `O` is lowercased, or `"unknown"` if empty.
 * @param backend Raw backend name (e.g. `"inmemory"`, `"postgres"`).
 * @param operation Operation name (e.g. `"get"`, `"create"`).
 * @return Normalized `{"backend", "operation"}` label map.
 */
std::map<std::string, std::string> repositoryMetricLabels(
    const std::string& backend, const std::string& operation);

/**
 * @brief Builds stable metric labels for cache operations.
 *
 * Uses the same normalization rules as `repositoryMetricLabels`.
 * @param backend Raw backend name.
 * @param operation Operation name.
 * @return Normalized `{"backend", "operation"}` label map.
 */
std::map<std::string, std::string> cacheMetricLabels(
    const std::string& backend, const std::string& operation);

/**
 * @brief Redacts sensitive config values before startup logging.
 *
 * Key matching is case-insensitive:
 * - Keys containing `"password"`, `"secret"`, or `"token"` are
 *   fully replaced with `"[REDACTED]"`.
 * - Keys containing `"dsn"` have only the password segment
 *   redacted: in URI form (`scheme://user:[REDACTED]\@host/db`)
 *   and in libpq keyword/value form (`password=[REDACTED]`);
 *   the rest of the DSN string is preserved.
 * - All other keys are returned unchanged.
 * @param key Config field name; matched case-insensitively.
 * @param value Raw field value to conditionally redact.
 * @return Fully redacted `"[REDACTED]"`, a partially redacted DSN
 * string, or `value` unchanged.
 */
std::string redactSecretValue(const std::string& key, const std::string& value);

}  // namespace observability
