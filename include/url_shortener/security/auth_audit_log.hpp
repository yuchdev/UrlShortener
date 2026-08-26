#pragma once

#include <chrono>
#include <optional>
#include <string>

/**
 * @brief Allowed audit log action values.
 */
namespace AuditAction {
    constexpr const char* PasswordVerify   = "password.verify";
    constexpr const char* SessionCreate    = "session.create";
    constexpr const char* TokenIntrospect  = "token.introspect";
    constexpr const char* TokenRevoke      = "token.revoke";
    constexpr const char* UserCreate       = "user.create";
    constexpr const char* UserDeactivate   = "user.deactivate";
    constexpr const char* FirstAdminCreate = "first_admin.create";
} // namespace AuditAction

/**
 * @brief Allowed audit log result values.
 */
namespace AuditResult {
    constexpr const char* Success = "success";
    constexpr const char* Failure = "failure";
    constexpr const char* Denied  = "denied";
} // namespace AuditResult

/**
 * @brief One audit log entry.
 *
 * Rules: never store passwords, hashes, raw tokens, or client secrets here.
 */
struct AuthAuditEntry {
    std::string id;
    std::optional<std::string> clientId;
    std::optional<std::string> username;
    std::optional<std::string> userId;
    std::string action;
    std::string result;
    std::optional<std::string> reason;
    std::chrono::system_clock::time_point createdAt;
};
