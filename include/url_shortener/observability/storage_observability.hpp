#pragma once

#include <map>
#include <string>

namespace observability {

/** Build stable metric labels for repository operations. Unknown values are normalized to "unknown". */
std::map<std::string, std::string> repositoryMetricLabels(const std::string& backend, const std::string& operation);

/** Build stable metric labels for cache operations. Unknown values are normalized to "unknown". */
std::map<std::string, std::string> cacheMetricLabels(const std::string& backend, const std::string& operation);

/** Redacts sensitive values (password-like and DSN credentials) before startup logging. */
std::string redactSecretValue(const std::string& key, const std::string& value);

} // namespace observability
