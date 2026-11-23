#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "url_shortener/security/control_set.hpp"
#include "url_shortener/security/user_credentials.hpp"

namespace soci
{
class session;
}

/**
 * @brief Persistence layer for `app_users`.
 *
 * All queries use prepared statements. The `passwordHash` field is
 * included in returned `UserCredentials` structs so that
 * `AuthBrokerService` can verify credentials; callers must not expose
 * it through external APIs.
 *
 * Lifetime: does not own `db`; the caller must keep the SOCI session
 * alive for at least as long as this object exists.
 */
class UserCredentialsRepository
{
public:
    /**
     * @brief Stores a reference to the SOCI session used for all
     * subsequent queries.
     * @param db Open SOCI session to the application database; must
     * outlive this object.
     */
    explicit UserCredentialsRepository(soci::session& db);

    /**
     * @brief Checks whether at least one active admin user exists.
     * @return `true` if the `app_users` table contains at least one
     * row with `control_set = 'admin'` and `is_active = 1`; `false`
     * otherwise.
     */
    bool activeAdminExists();

    /**
     * @brief Persists a new user credential record.
     * @param username The login name for the new user; must be unique
     * in `app_users`.
     * @param passwordHash Pre-hashed password produced by
     * `PasswordHasher`.
     * @param controlSet Role assigned to the new user.
     * @return The newly created `UserCredentials` record with all
     * fields populated.
     */
    UserCredentials createUser(const std::string& username,
                               const std::string& passwordHash,
                               ControlSet controlSet);

    /**
     * @brief Finds an active user by username, including the password
     * hash for credential verification.
     *
     * @param username The login name to search for.
     * @return The matching `UserCredentials` (including `passwordHash`)
     * if an active user with that name exists; `std::nullopt` if the
     * user is not found or is inactive.
     */
    std::optional<UserCredentials> findByUsername(const std::string& username);

    /**
     * @brief Finds an active user by their identifier.
     *
     * @param userId The opaque user identifier to search for.
     * @return The matching `UserCredentials` (including `passwordHash`)
     * if an active user with that ID exists; `std::nullopt` if the
     * user is not found or is inactive.
     */
    std::optional<UserCredentials> findById(const std::string& userId);

    /**
     * @brief Returns all users (active and inactive), ordered by
     * `created_at` ascending.
     * @return A vector of all `UserCredentials` records. The
     * `passwordHash` field is populated; callers must not expose it
     * through external APIs.
     */
    std::vector<UserCredentials> listUsers();

    /**
     * @brief Sets `is_active = 0` for the user identified by
     * `username`.
     *
     * A no-op at the database level if no user with that name exists.
     * @param username The login name of the user to deactivate.
     */
    void deactivateUser(const std::string& username);

    /**
     * @brief Updates `last_login_at` to the current time for the user
     * identified by `userId`.
     *
     * A no-op at the database level if no user with that ID exists.
     * @param userId The opaque identifier of the user to update.
     */
    void touchLastLogin(const std::string& userId);

private:
    ///< SOCI session used for all DML statements. Not owned; must
    ///< outlive this object.
    soci::session& db_;
};
