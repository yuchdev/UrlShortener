#pragma once

#include <string>

/**
 * @brief Generates and hashes cryptographically secure opaque tokens.
 */
class TokenGenerator {
public:
    /**
     * @brief Generate an opaque, cryptographically random token.
     * @param byteLength Number of random bytes before hex encoding.
     * @return Hex-encoded random token (length = 2 * byteLength).
     */
    std::string generateToken(std::size_t byteLength = 32) const;

    /**
     * @brief Hash a token for storage.
     * Only the hash must be stored; never the raw token.
     */
    std::string hashToken(const std::string& rawToken) const;
};
