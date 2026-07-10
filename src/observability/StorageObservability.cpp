#include "url_shortener/observability/StorageObservability.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

namespace {
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string normalizeBackend(const std::string& backend) {
    const auto b = lower(backend);
    if (b == "inmemory" || b == "sqlite" || b == "postgres" || b == "redis" || b == "none" || b == "noop") {
        return b;
    }
    return "unknown";
}

std::string normalizeOperation(const std::string& op) {
    const auto o = lower(op);
    if (o.empty()) return "unknown";
    return o;
}
} // namespace

namespace observability {

std::map<std::string, std::string> repositoryMetricLabels(const std::string& backend, const std::string& operation) {
    return {{"backend", normalizeBackend(backend)}, {"operation", normalizeOperation(operation)}};
}

std::map<std::string, std::string> cacheMetricLabels(const std::string& backend, const std::string& operation) {
    return {{"backend", normalizeBackend(backend)}, {"operation", normalizeOperation(operation)}};
}

std::string redactSecretValue(const std::string& key, const std::string& value) {
    const auto k = lower(key);
    if (k.find("password") != std::string::npos || k.find("secret") != std::string::npos || k.find("token") != std::string::npos) {
        return "[REDACTED]";
    }
    if (k.find("dsn") != std::string::npos) {
        auto out = value;
        // Handle URI form: protocol://user:[REDACTED]@host/db
        const auto pos = out.find("://");
        if (pos != std::string::npos) {
            const auto at = out.find('@', pos + 3);
            if (at != std::string::npos) {
                const auto colon = out.find(':', pos + 3);
                if (colon != std::string::npos && colon < at) {
                    out.replace(colon + 1, at - (colon + 1), "[REDACTED]");
                }
            }
        }
        // Handle libpq keyword/value form: key=value pairs, e.g. host=db dbname=urls
        static const std::regex kv_pw(R"((\bpassword\s*=\s*)('[^']*'|\S+))",
                                      std::regex_constants::icase);
        out = std::regex_replace(out, kv_pw, "$1[REDACTED]");
        return out;
    }
    return value;
}

} // namespace observability
