#pragma once

#include <string>

/**
 * @brief Hashes and verifies passwords.
 *
 * Current implementation: PBKDF2-SHA256 via OpenSSL.
 * Replace with Argon2id for production hardening.
 *
 * Rules:
 * - Never store or log plaintext passwords.
 * - Never return a hash through CLI or external API.
 */
class PasswordHasher {
public:
    /**
     * @brief Hash a plaintext password.
     * @return A non-empty encoded hash string suitable for storage.
     */
    std::string hashPassword(const std::string& password) const;

    /**
     * @brief Verify a plaintext password against a stored hash.
     * @return true if the password matches, false otherwise.
     */
    bool verifyPassword(const std::string& password,
                        const std::string& passwordHash) const;
};
