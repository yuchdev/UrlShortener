#pragma once

#include <stdexcept>
#include <string>

/**
 * @brief Application-level control sets (roles).
 *
 * admin — full application control.
 * user  — read-only access.
 */
enum class ControlSet {
    Admin,
    User,
};

std::string toString(ControlSet controlSet);
ControlSet  controlSetFromString(const std::string& value);

bool canRead(ControlSet controlSet);
bool canWrite(ControlSet controlSet);
bool canMigrate(ControlSet controlSet);
bool canManageUsers(ControlSet controlSet);
