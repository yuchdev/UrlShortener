#pragma once

#include <chrono>
#include <optional>
#include <string>

/**
 * @brief String constants for the `action` column of `auth_audit_log`.
 *
 * Use these constants exclusively; never hard-code raw strings in
 * `AuthAuditLogRepository::append` calls.
 */
namespace AuditAction
{
///< Password check for a user, successful or not.
constexpr const char* PasswordVerify = "password.verify";

///< Creation of a new authenticated session token.
constexpr const char* SessionCreate = "session.create";

///< Introspection / validation of an existing session token.
constexpr const char* TokenIntrospect = "token.introspect";

///< Explicit revocation of a session token.
constexpr const char* TokenRevoke = "token.revoke";

///< Creation of a new user account.
constexpr const char* UserCreate = "user.create";

///< Deactivation of an existing user account.
constexpr const char* UserDeactivate = "user.deactivate";

///< Bootstrap creation of the first admin user.
constexpr const char* FirstAdminCreate = "first_admin.create";
}  // namespace AuditAction

/**
 * @brief String constants for the `result` column of `auth_audit_log`.
 */
namespace AuditResult
{
///< The operation completed successfully.
constexpr const char* Success = "success";

///< The operation failed due to invalid credentials or an internal error.
constexpr const char* Failure = "failure";

///< The operation was refused because the caller lacked permission.
constexpr const char* Denied = "denied";
}  // namespace AuditResult

/**
 * @brief One audit log entry, as read back from `auth_audit_log`.
 *
 * Rules: never store passwords, hashes, raw tokens, or client secrets
 * here.
 */
struct AuthAuditEntry
{
    ///< Randomly generated opaque identifier for this entry.
    std::string id;

    ///< Identifier of the service client that triggered the operation;
    ///< absent when the operation did not originate from a service
    ///< client.
    std::optional<std::string> clientId;

    ///< Username involved in the operation; absent when not applicable
    ///< (e.g. a bare token introspection with no resolved user).
    std::optional<std::string> username;

    ///< Identifier of the resolved user; absent when the user could not
    ///< be resolved (e.g. a failed login where the username is unknown).
    std::optional<std::string> userId;

    ///< Action type; one of the `AuditAction::*` constants.
    std::string action;

    ///< Outcome; one of the `AuditResult::*` constants.
    std::string result;

    ///< Human-readable explanation for `Failure` or `Denied` outcomes;
    ///< absent on success.
    std::optional<std::string> reason;

    ///< Wall-clock time the entry was inserted.
    std::chrono::system_clock::time_point createdAt;
};
