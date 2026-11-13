#pragma once

#include <string>

/**
 * @brief Hashes and verifies passwords using PBKDF2-SHA256 (OpenSSL),
 * with a randomly generated per-password 16-byte salt and 100 000
 * iterations.
 *
 * Rules:
 * - Never store or log plaintext passwords.
 * - Never return a hash through CLI or external API.
 * - Replace with Argon2id for production hardening.
 */
class PasswordHasher
{
public:
    /**
     * @brief Hashes a plaintext password with a fresh random salt.
     * @param password The plaintext password to hash; never stored or
     * logged by this method.
     * @return A non-empty encoded hash string of the form
     * `"pbkdf2sha256:{iterations}:{saltHex}:{hashHex}"`, suitable for
     * storage and later verification via `verifyPassword`.
     * @throws std::runtime_error if the OpenSSL CSPRNG or PBKDF2
     * function fails.
     */
    std::string hashPassword(const std::string& password) const;

    /**
     * @brief Verifies a plaintext password against a stored hash.
     *
     * Uses a constant-time comparison to prevent timing-based attacks.
     * @param password The plaintext password to verify.
     * @param passwordHash The stored hash string produced by
     * `hashPassword`.
     * @return `true` if the password matches the hash; `false` if the
     * hash is malformed, uses an unsupported algorithm tag, or the
     * password does not match.
     */
    bool verifyPassword(const std::string& password,
                        const std::string& passwordHash) const;
};
