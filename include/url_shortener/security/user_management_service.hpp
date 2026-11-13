#pragma once

#include <string>
#include <vector>

#include "url_shortener/security/access_guard.hpp"
#include "url_shortener/security/auth_audit_log_repository.hpp"
#include "url_shortener/security/control_set.hpp"
#include "url_shortener/security/password_hasher.hpp"
#include "url_shortener/security/user_credentials.hpp"
#include "url_shortener/security/user_credentials_repository.hpp"

/**
 * @brief Admin-only user lifecycle operations.
 *
 * Every method calls `guard.requireUserManagement()` and throws
 * `AccessDeniedError` if the guard's control set is not `Admin`.
 *
 * Lifetime: does not own `users`, `passwordHasher`, or `auditLog`; the
 * caller must keep all three alive for at least as long as this object
 * exists.
 */
class UserManagementService
{
public:
    /**
     * @brief Stores references to collaborators; starts no background
     * work.
     * @param users User credential store; must outlive this object.
     * @param passwordHasher Used to hash plaintext passwords before
     * storage; must outlive this object.
     * @param auditLog Destination for user lifecycle audit entries;
     * must outlive this object.
     */
    UserManagementService(UserCredentialsRepository& users,
                          PasswordHasher& passwordHasher,
                          AuthAuditLogRepository& auditLog);

    /**
     * @brief Creates a new user account with the given role.
     *
     * Hashes `plainPassword` before storage; appends an
     * `AuditAction::UserCreate` / `AuditResult::Success` entry.
     * @param guard Must permit user management (i.e. hold `Admin`).
     * @param username The login name for the new user; must be unique
     * in `app_users`.
     * @param plainPassword The plaintext password; hashed before
     * storage, never logged.
     * @param controlSet Role to assign to the new account.
     * @return The newly created `UserCredentials` record.
     * @throws AccessDeniedError if `guard` does not permit user
     * management.
     */
    UserCredentials createUser(const AccessGuard& guard,
                               const std::string& username,
                               const std::string& plainPassword,
                               ControlSet controlSet);

    /**
     * @brief Returns all users known to the credential store.
     * @param guard Must permit user management (i.e. hold `Admin`).
     * @return All `UserCredentials` records (active and inactive),
     * ordered by `created_at` ascending.
     * @throws AccessDeniedError if `guard` does not permit user
     * management.
     */
    std::vector<UserCredentials> listUsers(const AccessGuard& guard);

    /**
     * @brief Deactivates the account identified by `username`.
     *
     * Appends an `AuditAction::UserDeactivate` / `AuditResult::Success`
     * entry. A no-op at the repository level if no user with that name
     * exists.
     * @param guard Must permit user management (i.e. hold `Admin`).
     * @param username The login name of the user to deactivate.
     * @throws AccessDeniedError if `guard` does not permit user
     * management.
     */
    void deactivateUser(const AccessGuard& guard, const std::string& username);

private:
    ///< User credential store; not owned, must outlive this object.
    UserCredentialsRepository& users_;

    ///< Password hasher; not owned, must outlive this object.
    PasswordHasher& passwordHasher_;

    ///< Audit log sink; not owned, must outlive this object.
    AuthAuditLogRepository& auditLog_;
};
