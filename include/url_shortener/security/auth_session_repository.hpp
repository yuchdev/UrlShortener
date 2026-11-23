#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "url_shortener/security/auth_session.hpp"

namespace soci
{
class session;
}

/**
 * @brief Persistence layer for `auth_sessions`.
 *
 * Lifetime: does not own `db`; the caller must keep the SOCI session
 * alive for at least as long as this object exists.
 */
class AuthSessionRepository
{
public:
    /**
     * @brief Stores a reference to the SOCI session used for all
     * subsequent queries.
     * @param db Open SOCI session to the application database; must
     * outlive this object.
     */
    explicit AuthSessionRepository(soci::session& db);

    /**
     * @brief Inserts a new session record and returns the populated
     * `AuthSession`.
     *
     * Assigns a randomly generated hex ID and sets `created_at` to the
     * current wall-clock time. The raw token must never be passed here;
     * only its pre-computed hash.
     * @param userId Identifier of the user this session belongs to.
     * @param tokenHash SHA-256 hex-encoded hash of the raw session
     * token.
     * @param expiresAt Wall-clock time after which the session expires.
     * @param clientId Optional service-client identifier; pass `nullptr`
     * when not applicable.
     * @return The newly created `AuthSession` with all fields populated.
     */
    AuthSession createSession(const std::string& userId,
                              const std::string& tokenHash,
                              std::chrono::system_clock::time_point expiresAt,
                              const std::string* clientId = nullptr);

    /**
     * @brief Looks up a session by its token hash and updates
     * `last_used_at` if the session is currently active.
     * @param tokenHash SHA-256 hex-encoded hash of the raw token to
     * find.
     * @return The matching `AuthSession` if a row with that token hash
     * exists; `std::nullopt` if no session was found.
     */
    std::optional<AuthSession> findByTokenHash(const std::string& tokenHash);

    /**
     * @brief Sets `revoked_at` to the current time for every session
     * whose `token_hash` matches `tokenHash`.
     *
     * A no-op at the database level if no matching session exists.
     * @param tokenHash SHA-256 hex-encoded hash of the token to revoke.
     */
    void revokeByTokenHash(const std::string& tokenHash);

    /**
     * @brief Sets `revoked_at` to the current time for all
     * non-revoked sessions belonging to `userId`.
     * @param userId Identifier of the user whose active sessions are
     * to be revoked.
     */
    void revokeAllForUser(const std::string& userId);

private:
    ///< SOCI session used for all DML statements. Not owned; must
    ///< outlive this object.
    soci::session& db_;
};
