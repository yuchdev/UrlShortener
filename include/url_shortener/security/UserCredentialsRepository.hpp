#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "url_shortener/security/ControlSet.hpp"
#include "url_shortener/security/UserCredentials.hpp"

namespace soci { class session; }

/**
 * @brief Persistence layer for app_users.
 *
 * All queries use prepared statements.
 * password_hash is never returned through public accessors.
 */
class UserCredentialsRepository {
public:
    explicit UserCredentialsRepository(soci::session& db);

    /** @return true if at least one active admin user exists. */
    bool activeAdminExists();

    /**
     * @brief Persist a new user credential record.
     * @param passwordHash Pre-hashed password (use PasswordHasher).
     */
    UserCredentials createUser(const std::string& username,
                               const std::string& passwordHash,
                               ControlSet          controlSet);

    /**
     * @brief Find a user by username including the password hash for verification.
     *
     * Returns nullopt if user is not found or is inactive.
     */
    std::optional<UserCredentials> findByUsername(const std::string& username);

    /**
     * @brief Find a user by their ID.
     *
     * Returns nullopt if user is not found or is inactive.
     */
    std::optional<UserCredentials> findById(const std::string& userId);

    std::vector<UserCredentials> listUsers();

    void deactivateUser(const std::string& username);

    /** @brief Update last_login_at for a user. */
    void touchLastLogin(const std::string& userId);

private:
    soci::session& db_;
};
