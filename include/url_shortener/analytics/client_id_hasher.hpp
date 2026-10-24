#pragma once

#include <cstddef>
#include <string>

#include "url_shortener/analytics/analytics_error.hpp"

namespace url_shortener::analytics
{

/**
 * @brief Configuration for deriving anonymized client identifier hashes.
 */
struct ClientIdHashConfig
{
    ///< HMAC-SHA256 key (typically `AnalyticsConfig::client_hash_salt`);
    ///< `Hash` fails if empty. Never log this value.
    std::string salt;

    ///< If `true`, an empty `normalized_identifier` passed to `Hash` 
    ///< yields an empty result with no error (used in non-production mode via `AnalyticsService`);
    ///< if `false`, it is a hashing failure.
    bool allow_missing_identifier = false;

    ///< Maximum length, in hex characters, of the returned digest;
    ///< longer output is truncated. `0` means "no truncation".
    ///< The default of 64 matches the full-length SHA-256 hex digest.
    std::size_t output_length = 64;
};

/**
 * @brief Produces deterministic keyed hashes for client network
 * identifiers, so the raw identifier (e.g. an IP address) is never stored.
 */
class ClientIdHasher
{
public:

    /**
     * @brief Computes an HMAC-SHA256 hex digest of `normalized_identifier`
     * keyed by `config.salt`, truncated to `config.output_length` hex
     * characters.
     * @param normalized_identifier Raw client identifier; expected to
     * already be normalized by the caller (this method does not validate
     * its format). May be empty - see `config.allow_missing_identifier`.
     * @param config Hashing configuration.
     * @param[out] error If non-null and the call fails, receives
     * `AnalyticsErrorCode::validation` with a message describing the
     * failure ("Missing salt" or "Missing identifier"); left untouched
     * on success - including the "allowed missing identifier" case below.
     * @return The hex-encoded digest, truncated per
     * `config.output_length`. Returns an empty string both on failure
     * (`config.salt` empty, or `normalized_identifier` empty without
     * `config.allow_missing_identifier`) and as a valid non-failure result
     * (`normalized_identifier` empty with `config.allow_missing_identifier
     * == true`) - callers must check `error->code` (when a non-null
     * `error` was passed), not just whether the return value is empty, to
     * distinguish the two.
     */
    static std::string Hash(const std::string& normalized_identifier,
                            const ClientIdHashConfig& config,
                            AnalyticsError* error = nullptr);
};

}  // namespace url_shortener::analytics
