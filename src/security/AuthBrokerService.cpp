#include "url_shortener/security/AuthBrokerService.hpp"

#include "url_shortener/security/AuthAuditLog.hpp"

AuthBrokerService::AuthBrokerService(UserCredentialsRepository& users,
                                     AuthSessionRepository&      sessions,
                                     PasswordHasher&             passwordHasher,
                                     TokenGenerator&             tokenGenerator,
                                     AuthAuditLogRepository&     auditLog,
                                     std::chrono::seconds        sessionLifetime)
    : users_(users)
    , sessions_(sessions)
    , passwordHasher_(passwordHasher)
    , tokenGenerator_(tokenGenerator)
    , auditLog_(auditLog)
    , sessionLifetime_(sessionLifetime)
{
}

VerifyResult AuthBrokerService::verifyPassword(const std::string& username,
                                               const std::string& plainPassword)
{
    auto userOpt = users_.findByUsername(username);

    // Use a generic failure regardless of whether username or password is wrong
    // to avoid leaking which field failed.
    if (!userOpt || !userOpt->isActive) {
        auditLog_.append(AuditAction::PasswordVerify,
                         AuditResult::Failure,
                         &username);
        return VerifyResult{false, std::nullopt};
    }

    const bool ok = passwordHasher_.verifyPassword(plainPassword, userOpt->passwordHash);
    if (!ok) {
        auditLog_.append(AuditAction::PasswordVerify,
                         AuditResult::Failure,
                         &username);
        return VerifyResult{false, std::nullopt};
    }

    users_.touchLastLogin(userOpt->id);
    auditLog_.append(AuditAction::PasswordVerify,
                     AuditResult::Success,
                     &username,
                     &userOpt->id);
    return VerifyResult{true, userOpt};
}

CreateSessionResult AuthBrokerService::createSession(const std::string& username,
                                                      const std::string& plainPassword,
                                                      const std::string* clientId)
{
    auto verifyRes = verifyPassword(username, plainPassword);
    if (!verifyRes.ok) {
        auditLog_.append(AuditAction::SessionCreate,
                         AuditResult::Failure,
                         &username,
                         nullptr,
                         clientId);
        return CreateSessionResult{false, {}, std::nullopt, std::nullopt};
    }

    const std::string rawToken  = tokenGenerator_.generateToken(32);
    const std::string tokenHash = tokenGenerator_.hashToken(rawToken);
    const auto        expiresAt = std::chrono::system_clock::now() + sessionLifetime_;

    AuthSession session = sessions_.createSession(verifyRes.user->id,
                                                   tokenHash,
                                                   expiresAt,
                                                   clientId);

    auditLog_.append(AuditAction::SessionCreate,
                     AuditResult::Success,
                     &username,
                     &verifyRes.user->id,
                     clientId);

    CreateSessionResult result;
    result.ok       = true;
    result.rawToken = rawToken;
    result.session  = session;
    result.user     = verifyRes.user;
    return result;
}

IntrospectResult AuthBrokerService::introspectToken(const std::string& rawToken)
{
    const std::string tokenHash = tokenGenerator_.hashToken(rawToken);
    auto sessionOpt = sessions_.findByTokenHash(tokenHash);

    if (!sessionOpt) {
        auditLog_.append(AuditAction::TokenIntrospect, AuditResult::Failure);
        return IntrospectResult{false, std::nullopt, std::nullopt};
    }

    const auto now = std::chrono::system_clock::now();
    if (!sessionOpt->isActive(now)) {
        auditLog_.append(AuditAction::TokenIntrospect, AuditResult::Failure);
        return IntrospectResult{false, std::nullopt, std::nullopt};
    }

    auto userOpt = users_.findById(sessionOpt->userId);
    if (!userOpt) {
        auditLog_.append(AuditAction::TokenIntrospect, AuditResult::Failure);
        return IntrospectResult{false, std::nullopt, std::nullopt};
    }

    auditLog_.append(AuditAction::TokenIntrospect,
                     AuditResult::Success,
                     &userOpt->username,
                     &userOpt->id);

    IntrospectResult result;
    result.active  = true;
    result.user    = userOpt;
    result.session = sessionOpt;
    return result;
}

void AuthBrokerService::revokeToken(const std::string& rawToken)
{
    const std::string tokenHash = tokenGenerator_.hashToken(rawToken);
    sessions_.revokeByTokenHash(tokenHash);
    auditLog_.append(AuditAction::TokenRevoke, AuditResult::Success);
}

// static
AccessGuard AuthBrokerService::guardFor(const UserCredentials& user)
{
    return AccessGuard(user.controlSet);
}
