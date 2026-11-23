#pragma once

#include <stdexcept>
#include <string>

/**
 * @brief Application-level control sets (roles) governing which
 * operations a credential holder may perform.
 *
 * Use `canRead`, `canWrite`, `canMigrate`, and `canManageUsers` to
 * query capabilities rather than comparing enum values directly.
 */
enum class ControlSet
{
    ///< Full application control: read, write, migrate, and manage
    ///< users.
    Admin,

    ///< Read-only access; write, migration, and user-management
    ///< operations are denied.
    User,
};

/**
 * @brief Converts a `ControlSet` value to its lowercase string
 * representation.
 * @param controlSet Value to convert.
 * @return `"admin"` for `Admin`, `"user"` for `User`.
 * @throws std::invalid_argument if `controlSet` holds an unrecognized
 * value.
 */
std::string toString(ControlSet controlSet);

/**
 * @brief Parses a string into a `ControlSet` value.
 * @param value Case-sensitive string to parse; `"admin"` or `"user"`.
 * @return The corresponding `ControlSet` enumerator.
 * @throws std::invalid_argument if `value` is neither `"admin"` nor
 * `"user"`.
 */
ControlSet controlSetFromString(const std::string& value);

/**
 * @brief Returns whether the control set permits read operations.
 * @param controlSet The role to query.
 * @return `true` for all current control sets (`Admin` and `User` can
 * both read).
 */
bool canRead(ControlSet controlSet);

/**
 * @brief Returns whether the control set permits write (mutating)
 * operations.
 * @param controlSet The role to query.
 * @return `true` only for `Admin`; `false` for `User`.
 */
bool canWrite(ControlSet controlSet);

/**
 * @brief Returns whether the control set permits database migration
 * operations.
 * @param controlSet The role to query.
 * @return `true` only for `Admin`; `false` for `User`.
 */
bool canMigrate(ControlSet controlSet);

/**
 * @brief Returns whether the control set permits user management
 * operations.
 * @param controlSet The role to query.
 * @return `true` only for `Admin`; `false` for `User`.
 */
bool canManageUsers(ControlSet controlSet);
