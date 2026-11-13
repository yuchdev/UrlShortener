#pragma once

#include <chrono>
#include <optional>
#include <string>

/**
 * @brief Active or expired auth session record.
 *
 * Raw tokens are never stored; only the SHA-256 hash of the token is
 * persisted in `tokenHash`.
 */
struct AuthSession
{
    ///< Randomly generated opaque identifier for this session record.
    std::string id;

    ///< Identifier of the user this session belongs to.
    std::string userId;

    ///< Optional service-client identifier recorded at session creation;
    ///< absent for sessions created without a `clientId`.
    std::optional<std::string> clientId;

    ///< SHA-256 hex-encoded hash of the raw session token; the raw
    ///< token is never stored.
    std::string tokenHash;

    ///< Wall-clock time the session was created.
    std::chrono::system_clock::time_point createdAt;

    ///< Wall-clock time after which the session is considered expired
    ///< and `isActive` returns `false`.
    std::chrono::system_clock::time_point expiresAt;

    ///< Wall-clock time the session was explicitly revoked; absent if
    ///< the session has not been revoked.
    std::optional<std::chrono::system_clock::time_point> revokedAt;

    ///< Wall-clock time of the most recent successful token lookup;
    ///< updated by `AuthSessionRepository::findByTokenHash` while the
    ///< session is active. Absent until the first lookup.
    std::optional<std::chrono::system_clock::time_point> lastUsedAt;

    /**
     * @brief Checks whether the session is currently usable.
     * @param now Current wall-clock time to compare against `expiresAt`.
     * @return `true` if the session has not been revoked and `now` is
     * strictly before `expiresAt`; `false` if `revokedAt` is set or
     * `now >= expiresAt`.
     */
    bool isActive(std::chrono::system_clock::time_point now) const;
};
