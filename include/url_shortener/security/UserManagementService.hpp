#pragma once

#include <string>
#include <vector>

#include "url_shortener/security/AccessGuard.hpp"
#include "url_shortener/security/AuthAuditLogRepository.hpp"
#include "url_shortener/security/ControlSet.hpp"
#include "url_shortener/security/PasswordHasher.hpp"
#include "url_shortener/security/UserCredentials.hpp"
#include "url_shortener/security/UserCredentialsRepository.hpp"

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
