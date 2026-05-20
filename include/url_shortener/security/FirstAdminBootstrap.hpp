#pragma once

#include <stdexcept>
#include <string>

#include "url_shortener/security/AuthAuditLogRepository.hpp"
#include "url_shortener/security/PasswordHasher.hpp"
#include "url_shortener/security/UserCredentials.hpp"
#include "url_shortener/security/UserCredentialsRepository.hpp"

/**
 * @brief Thrown when an admin already exists at bootstrap time.
 */
class AdminAlreadyExistsError : public std::runtime_error {
public:
    AdminAlreadyExistsError();
};

/**
 * @brief One-time creation of the first admin user.
 *
 * This is the only path that can create an admin without prior auth.
 * Fails if any active admin already exists.
 */
class FirstAdminBootstrap {
public:
    FirstAdminBootstrap(UserCredentialsRepository& users,
                        PasswordHasher&             passwordHasher,
                        AuthAuditLogRepository&     auditLog);

    /**
     * @brief Create the first admin user.
     *
     * @param username       Plain username.
     * @param plainPassword  Plain password — hashed before storage, never logged.
     * @return The created UserCredentials record (passwordHash field omitted from display).
     * @throws AdminAlreadyExistsError if any active admin already exists.
     */
    UserCredentials createFirstAdmin(const std::string& username,
                                     const std::string& plainPassword);

private:
    UserCredentialsRepository& users_;
    PasswordHasher&             passwordHasher_;
    AuthAuditLogRepository&     auditLog_;
};
