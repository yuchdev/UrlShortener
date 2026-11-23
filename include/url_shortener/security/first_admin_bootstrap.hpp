#pragma once

#include <stdexcept>
#include <string>

#include "url_shortener/security/auth_audit_log_repository.hpp"
#include "url_shortener/security/password_hasher.hpp"
#include "url_shortener/security/user_credentials.hpp"
#include "url_shortener/security/user_credentials_repository.hpp"

/**
 * @brief Thrown when an admin already exists at bootstrap time.
 */
class AdminAlreadyExistsError : public std::runtime_error
{
public:
    /**
     * @brief Constructs the error with a fixed message indicating that
     * at least one active admin already exists.
     */
    AdminAlreadyExistsError();
};

/**
 * @brief One-time creation of the first admin user.
 *
 * This is the only path that can create an admin without prior
 * authentication. Fails if any active admin already exists.
 *
 * Lifetime: does not own `users`, `passwordHasher`, or `auditLog`; the
 * caller must keep all three alive for at least as long as this object
 * exists.
 */
class FirstAdminBootstrap
{
public:
    /**
     * @brief Stores references to collaborators; starts no background
     * work.
     * @param users User credential store; must outlive this object.
     * @param passwordHasher Used to hash the plaintext password before
     * storage; must outlive this object.
     * @param auditLog Destination for the bootstrap audit entry; must
     * outlive this object.
     */
    FirstAdminBootstrap(UserCredentialsRepository& users,
                        PasswordHasher& passwordHasher,
                        AuthAuditLogRepository& auditLog);

    /**
     * @brief Creates the first admin user.
     *
     * @param username      Plain username to register.
     * @param plainPassword Plain password; hashed before storage, never
     * logged.
     * @return The created `UserCredentials` record. Callers must not
     * expose `passwordHash` through CLI output or external APIs.
     * @throws AdminAlreadyExistsError if any active admin already
     * exists.
     */
    UserCredentials createFirstAdmin(const std::string& username,
                                     const std::string& plainPassword);

private:
    ///< User credential store; not owned, must outlive this object.
    UserCredentialsRepository& users_;

    ///< Password hasher for the new admin's credential; not owned,
    ///< must outlive this object.
    PasswordHasher& passwordHasher_;

    ///< Audit log sink; not owned, must outlive this object.
    AuthAuditLogRepository& auditLog_;
};
