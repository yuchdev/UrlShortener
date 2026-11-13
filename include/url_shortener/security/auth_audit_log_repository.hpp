#pragma once

#include <optional>
#include <string>

#include "url_shortener/security/auth_audit_log.hpp"

namespace soci
{
class session;
}

/**
 * @brief Append-only persistence layer for `auth_audit_log`.
 *
 * Lifetime: does not own `db`; the caller must keep the SOCI session
 * alive for at least as long as this object exists.
 */
class AuthAuditLogRepository
{
public:
    /**
     * @brief Stores a reference to the SOCI session used for all
     * subsequent queries.
     * @param db Open SOCI session to the application database; must
     * outlive this object.
     */
    explicit AuthAuditLogRepository(soci::session& db);

    /**
     * @brief Inserts one entry into `auth_audit_log`.
     *
     * A randomly generated hex ID and the current wall-clock time are
     * assigned automatically; the caller supplies the remaining fields.
     * @param action Action type; use an `AuditAction::*` constant.
     * @param result Outcome; use an `AuditResult::*` constant.
     * @param username If non-null, written to the `username` column;
     * otherwise the column is left `NULL`.
     * @param userId If non-null, written to the `user_id` column;
     * otherwise the column is left `NULL`.
     * @param clientId If non-null, written to the `client_id` column;
     * otherwise the column is left `NULL`.
     * @param reason If non-null, written to the `reason` column;
     * otherwise the column is left `NULL`.
     */
    void append(const std::string& action,
                const std::string& result,
                const std::string* username = nullptr,
                const std::string* userId = nullptr,
                const std::string* clientId = nullptr,
                const std::string* reason = nullptr);

private:
    ///< SOCI session used for all INSERT statements. Not owned; must
    ///< outlive this object.
    soci::session& db_;
};
