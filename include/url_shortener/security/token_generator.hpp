#pragma once

#include <string>

/**
 * @brief Generates and hashes cryptographically secure opaque tokens.
 */
class TokenGenerator
{
public:
    /**
     * @brief Generates an opaque, cryptographically random token.
     * @param byteLength Number of random bytes before hex encoding.
     * @return Hex-encoded random token of exactly `2 * byteLength`
     * characters.
     * @throws std::runtime_error if the OpenSSL CSPRNG fails.
     */
    std::string generateToken(std::size_t byteLength = 32) const;

    /**
     * @brief Computes the SHA-256 hash of a token for storage.
     *
     * Only the hash must be stored; the raw token must never be
     * persisted or logged.
     * @param rawToken The raw token string to hash.
     * @return Hex-encoded SHA-256 digest of `rawToken` (64 characters).
     */
    std::string hashToken(const std::string& rawToken) const;
};
