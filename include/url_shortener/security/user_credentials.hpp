#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "url_shortener/security/control_set.hpp"

/**
 * @brief Persisted user credential record.
 *
 * `passwordHash` must never be returned through CLI or external API.
 */
struct UserCredentials
{
    ///< Randomly generated opaque database identifier.
    std::string id;

    ///< Human-readable login name; unique in `app_users`.
    std::string username;

    ///< PBKDF2-SHA256 encoded password hash of the form
    ///< `"pbkdf2sha256:{iterations}:{saltHex}:{hashHex}"`. Never
    ///< expose through CLI or external API.
    std::string passwordHash;

    ///< Role controlling which operations this user may perform.
    ControlSet controlSet {ControlSet::User};

    ///< Whether the account is currently active; inactive accounts are
    ///< not returned by `findByUsername` or `findById`.
    bool isActive {true};

    ///< Wall-clock time the account was created.
    std::chrono::system_clock::time_point createdAt;

    ///< Wall-clock time the account record was last modified.
    std::chrono::system_clock::time_point updatedAt;

    ///< Wall-clock time of the most recent successful login; absent
    ///< until the first login.
    std::optional<std::chrono::system_clock::time_point> lastLoginAt;
};
