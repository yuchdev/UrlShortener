#pragma once

#include <cstddef>
#include <string>

#include "url_shortener/analytics/AnalyticsError.hpp"

namespace url_shortener::analytics {

/**
 * @brief Configuration for deriving anonymized client identifier hashes.
 */
struct ClientIdHashConfig {
    std::string salt;
    bool allow_missing_identifier = false;
    std::size_t output_length = 64;
};

/**
 * @brief Produces deterministic keyed hashes for client network identifiers.
 */
class ClientIdHasher {
public:
    static std::string Hash(const std::string& normalized_identifier, const ClientIdHashConfig& config, AnalyticsError* error = nullptr);
};

} // namespace url_shortener::analytics
