#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "url_shortener/security/AccessGuard.hpp"
#include "url_shortener/security/AuthAuditLogRepository.hpp"
#include "url_shortener/security/AuthSession.hpp"
#include "url_shortener/security/AuthSessionRepository.hpp"
#include "url_shortener/security/ControlSet.hpp"
#include "url_shortener/security/PasswordHasher.hpp"
#include "url_shortener/security/TokenGenerator.hpp"
#include "url_shortener/security/UserCredentials.hpp"
#include "url_shortener/security/UserCredentialsRepository.hpp"

/**
 * @brief Result of a password verification.
 */
struct VerifyResult {
    bool ok{false};
    std::optional<UserCredentials> user;
};

/**
 * @brief Result of session creation.
 */
struct CreateSessionResult {
    bool ok{false};
    std::string rawToken;  ///< Return to caller; do not store; do not log.
    std::optional<AuthSession> session;
    std::optional<UserCredentials> user;
};

/**
 * @brief Result of token introspection.
 */
struct IntrospectResult {
    bool active{false};
    std::optional<UserCredentials> user;
    std::optional<AuthSession> session;
};

/**
 * @brief Local-only authentication broker.
 *
 * Verifies passwords, creates opaque sessions, and introspects tokens.
 * Must never be exposed to the public internet.
 *
 * Security invariants:
 * - Passwords accepted only in-process or via request body.
 * - Raw tokens are never stored; only hashes.
 * - Failed credential checks do not reveal which field was wrong.
 * - Audit log entries are written for all operations.
 */
class AuthBrokerService {
public:
    AuthBrokerService(UserCredentialsRepository& users,
                      AuthSessionRepository&      sessions,
                      PasswordHasher&             passwordHasher,
                      TokenGenerator&             tokenGenerator,
                      AuthAuditLogRepository&     auditLog,
                      std::chrono::seconds        sessionLifetime = std::chrono::hours(1));

    VerifyResult verifyPassword(const std::string& username,
                                const std::string& plainPassword);

    CreateSessionResult createSession(const std::string& username,
                                      const std::string& plainPassword,
                                      const std::string* clientId = nullptr);

    IntrospectResult introspectToken(const std::string& rawToken);

    void revokeToken(const std::string& rawToken);

    /** @brief Build an AccessGuard from a successfully authenticated user. */
    static AccessGuard guardFor(const UserCredentials& user);

private:
    UserCredentialsRepository& users_;
    AuthSessionRepository&     sessions_;
    PasswordHasher&             passwordHasher_;
    TokenGenerator&             tokenGenerator_;
    AuthAuditLogRepository&     auditLog_;
    std::chrono::seconds        sessionLifetime_;
};
