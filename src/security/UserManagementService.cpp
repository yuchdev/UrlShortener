#include "url_shortener/security/UserManagementService.hpp"

#include "url_shortener/security/AuthAuditLog.hpp"

UserManagementService::UserManagementService(UserCredentialsRepository& users,
                                             PasswordHasher&             passwordHasher,
                                             AuthAuditLogRepository&     auditLog)
    : users_(users)
    , passwordHasher_(passwordHasher)
    , auditLog_(auditLog)
{
}

UserCredentials UserManagementService::createUser(const AccessGuard& guard,
                                                   const std::string& username,
                                                   const std::string& plainPassword,
                                                   ControlSet          controlSet)
{
    guard.requireUserManagement();

    const std::string passwordHash = passwordHasher_.hashPassword(plainPassword);
    UserCredentials   user         = users_.createUser(username, passwordHash, controlSet);

    auditLog_.append(AuditAction::UserCreate,
                     AuditResult::Success,
                     &username,
                     &user.id);
    return user;
}

std::vector<UserCredentials> UserManagementService::listUsers(const AccessGuard& guard)
{
    guard.requireUserManagement();
    return users_.listUsers();
}

void UserManagementService::deactivateUser(const AccessGuard& guard,
                                           const std::string& username)
{
    guard.requireUserManagement();
    users_.deactivateUser(username);

    auditLog_.append(AuditAction::UserDeactivate,
                     AuditResult::Success,
                     &username);
}
