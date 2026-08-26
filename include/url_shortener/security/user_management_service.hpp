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
 * All mutating operations require requireUserManagement() on the guard.
 */
class UserManagementService {
public:
    UserManagementService(UserCredentialsRepository& users,
                          PasswordHasher&             passwordHasher,
                          AuthAuditLogRepository&     auditLog);

    UserCredentials createUser(const AccessGuard& guard,
                               const std::string& username,
                               const std::string& plainPassword,
                               ControlSet          controlSet);

    std::vector<UserCredentials> listUsers(const AccessGuard& guard);

    void deactivateUser(const AccessGuard& guard,
                        const std::string& username);

private:
    UserCredentialsRepository& users_;
    PasswordHasher&             passwordHasher_;
    AuthAuditLogRepository&     auditLog_;
};
