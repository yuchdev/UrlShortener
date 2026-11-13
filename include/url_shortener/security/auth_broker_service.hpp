#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "url_shortener/security/access_guard.hpp"
#include "url_shortener/security/auth_audit_log_repository.hpp"
#include "url_shortener/security/auth_session.hpp"
#include "url_shortener/security/auth_session_repository.hpp"
#include "url_shortener/security/control_set.hpp"
#include "url_shortener/security/password_hasher.hpp"
#include "url_shortener/security/token_generator.hpp"
#include "url_shortener/security/user_credentials.hpp"
#include "url_shortener/security/user_credentials_repository.hpp"

/**
 * @brief Result of a password verification attempt.
 */
struct VerifyResult
{
    ///< `true` if the credentials matched an active account; `false`
    ///< otherwise.
    bool ok {false};

    ///< Populated with the matching `UserCredentials` on success;
    ///< `std::nullopt` on failure.
    std::optional<UserCredentials> user;
};

/**
 * @brief Result of a session creation request.
 */
struct CreateSessionResult
{
    ///< `true` if password verification succeeded and the session was
    ///< created; `false` otherwise.
    bool ok {false};

    ///< The raw (unhashed) session token to return to the caller.
    ///< Must not be stored or logged; valid only when `ok` is `true`.
    std::string rawToken;

    ///< The persisted session record; populated only when `ok` is
    ///< `true`.
    std::optional<AuthSession> session;

    ///< The authenticated user record; populated only when `ok` is
    ///< `true`.
    std::optional<UserCredentials> user;
};

/**
 * @brief Result of a token introspection request.
 */
struct IntrospectResult
{
    ///< `true` if the token maps to an active, unexpired session backed
    ///< by a known active user; `false` otherwise.
    bool active {false};

    ///< The resolved user; populated only when `active` is `true`.
    std::optional<UserCredentials> user;

    ///< The session the token maps to; populated only when `active` is
    ///< `true`.
    std::optional<AuthSession> session;
};

/**
 * @brief Local-only authentication broker.
 *
 * Verifies passwords, creates opaque sessions, and introspects tokens.
 * Must never be exposed to the public internet; intended for Unix
 * domain socket or loopback HTTP only.
 *
 * Security invariants:
 * - Passwords accepted only in-process or via request body.
 * - Raw tokens are never stored; only their SHA-256 hash is persisted.
 * - Failed credential checks always return a generic failure, never
 *   revealing which field (username or password) was wrong.
 * - Every operation writes an entry to `auth_audit_log`.
 *
 * Lifetime: does not own any constructor argument; `users`, `sessions`,
 * `passwordHasher`, `tokenGenerator`, and `auditLog` must all outlive
 * this object.
 */
class AuthBrokerService
{
public:
    /**
     * @brief Stores references to collaborators; starts no background
     * work.
     * @param users Source of user credential records; must outlive this
     * object.
     * @param sessions Session persistence; must outlive this object.
     * @param passwordHasher Used to verify plaintext passwords against
     * stored hashes; must outlive this object.
     * @param tokenGenerator Used to generate and hash session tokens;
     * must outlive this object.
     * @param auditLog Destination for all authentication audit entries;
     * must outlive this object.
     * @param sessionLifetime Validity window of newly created sessions;
     * defaults to one hour.
     */
    AuthBrokerService(
        UserCredentialsRepository& users,
        AuthSessionRepository& sessions,
        PasswordHasher& passwordHasher,
        TokenGenerator& tokenGenerator,
        AuthAuditLogRepository& auditLog,
        std::chrono::seconds sessionLifetime = std::chrono::hours(1));

    /**
     * @brief Looks up `username` and verifies `plainPassword` against
     * the stored PBKDF2-SHA256 hash.
     *
     * Both success and failure are written to `auth_audit_log`. The
     * result is identical whether the username does not exist, the
     * account is inactive, or the password is wrong.
     * @param username The username to look up.
     * @param plainPassword The plaintext password to verify; never
     * stored or logged.
     * @return `VerifyResult` with `ok == true` and `user` populated if
     * the credentials matched an active account; `ok == false` with
     * `user == std::nullopt` otherwise.
     */
    VerifyResult verifyPassword(const std::string& username,
                                const std::string& plainPassword);

    /**
     * @brief Verifies credentials and, on success, creates a new
     * session token with the configured lifetime.
     *
     * Internally calls `verifyPassword`; both success and failure are
     * written to `auth_audit_log`. On success, a 64-character
     * hex-encoded cryptographically random token is generated; only
     * its SHA-256 hash is stored.
     * @param username The username to authenticate.
     * @param plainPassword The plaintext password; never stored or
     * logged.
     * @param clientId Optional service-client identifier recorded in
     * the session and the audit log; pass `nullptr` when not
     * applicable.
     * @return `CreateSessionResult` with `ok == true`, `rawToken`,
     * `session`, and `user` populated on success; `ok == false` with
     * the other fields empty/null if credential verification failed.
     * The raw token is never retrievable after this call.
     */
    CreateSessionResult createSession(const std::string& username,
                                      const std::string& plainPassword,
                                      const std::string* clientId = nullptr);

    /**
     * @brief Looks up and validates the session associated with
     * `rawToken`.
     *
     * Hashes the raw token, finds the matching session, checks it is
     * active and unexpired, and verifies the owning user still exists
     * and is active. Writes to `auth_audit_log` regardless of outcome.
     * @param rawToken The raw (unhashed) session token to validate.
     * @return `IntrospectResult` with `active == true`, `user`, and
     * `session` populated if the token maps to a valid active session;
     * `active == false` with both fields null if the token is not
     * found, the session is expired or revoked, or the associated user
     * no longer exists.
     */
    IntrospectResult introspectToken(const std::string& rawToken);

    /**
     * @brief Revokes the session associated with `rawToken`, if any.
     *
     * Hashes the raw token and sets `revoked_at` on the matching
     * session record. A no-op if no session with that token hash
     * exists. Always writes an `AuditAction::TokenRevoke` /
     * `AuditResult::Success` entry.
     * @param rawToken The raw (unhashed) token of the session to
     * revoke.
     */
    void revokeToken(const std::string& rawToken);

    /**
     * @brief Builds an `AccessGuard` reflecting the given user's
     * control set.
     * @param user The authenticated user record.
     * @return An `AccessGuard` configured with `user.controlSet`.
     */
    static AccessGuard guardFor(const UserCredentials& user);

private:
    ///< User credential store; not owned, must outlive this object.
    UserCredentialsRepository& users_;

    ///< Session persistence; not owned, must outlive this object.
    AuthSessionRepository& sessions_;

    ///< Password verifier; not owned, must outlive this object.
    PasswordHasher& passwordHasher_;

    ///< Token generator and hasher; not owned, must outlive this
    ///< object.
    TokenGenerator& tokenGenerator_;

    ///< Audit log sink; not owned, must outlive this object.
    AuthAuditLogRepository& auditLog_;

    ///< Validity window applied to newly created sessions.
    std::chrono::seconds sessionLifetime_;
};
