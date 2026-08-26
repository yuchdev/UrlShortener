#include "url_shortener/security/first_admin_bootstrap.hpp"

#include "url_shortener/security/auth_audit_log.hpp"
#include "url_shortener/security/control_set.hpp"

AdminAlreadyExistsError::AdminAlreadyExistsError()
    : std::runtime_error("Cannot create first admin: at least one active admin already exists.")
{
}

FirstAdminBootstrap::FirstAdminBootstrap(UserCredentialsRepository& users,
                                         PasswordHasher&             passwordHasher,
                                         AuthAuditLogRepository&     auditLog)
    : users_(users)
    , passwordHasher_(passwordHasher)
    , auditLog_(auditLog)
{
}

UserCredentials FirstAdminBootstrap::createFirstAdmin(const std::string& username,
                                                       const std::string& plainPassword)
{
    if (users_.activeAdminExists()) {
        auditLog_.append(AuditAction::FirstAdminCreate,
                         AuditResult::Denied,
                         &username);
        throw AdminAlreadyExistsError();
    }

    const std::string passwordHash = passwordHasher_.hashPassword(plainPassword);
    UserCredentials   user         = users_.createUser(username, passwordHash, ControlSet::Admin);

    auditLog_.append(AuditAction::FirstAdminCreate,
                     AuditResult::Success,
                     &username,
                     &user.id);
    return user;
}
