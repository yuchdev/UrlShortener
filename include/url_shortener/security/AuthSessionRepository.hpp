#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "url_shortener/security/AuthSession.hpp"

namespace soci { class session; }

/**
 * @brief Persistence layer for auth_sessions.
 */
class AuthSessionRepository {
public:
    explicit AuthSessionRepository(soci::session& db);

    AuthSession createSession(const std::string& userId,
                              const std::string& tokenHash,
                              std::chrono::system_clock::time_point expiresAt,
                              const std::string* clientId = nullptr);

    /** @brief Look up a session by token hash. Updates last_used_at if active. */
    std::optional<AuthSession> findByTokenHash(const std::string& tokenHash);

    void revokeByTokenHash(const std::string& tokenHash);

    /** @brief Revoke all active sessions for a user. */
    void revokeAllForUser(const std::string& userId);

private:
    soci::session& db_;
};
