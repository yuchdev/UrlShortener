#include "url_shortener/security/ControlSet.hpp"

#include <stdexcept>

std::string toString(ControlSet controlSet)
{
    switch (controlSet) {
    case ControlSet::Admin: return "admin";
    case ControlSet::User:  return "user";
    }
    throw std::invalid_argument("Unknown ControlSet value");
}

ControlSet controlSetFromString(const std::string& value)
{
    if (value == "admin") return ControlSet::Admin;
    if (value == "user")  return ControlSet::User;
    throw std::invalid_argument("Unknown control_set value: " + value);
}

bool canRead(ControlSet controlSet)
{
    (void)controlSet;
    return true; // both admin and user can read
}

bool canWrite(ControlSet controlSet)
{
    return controlSet == ControlSet::Admin;
}

bool canMigrate(ControlSet controlSet)
{
    return controlSet == ControlSet::Admin;
}

bool canManageUsers(ControlSet controlSet)
{
    return controlSet == ControlSet::Admin;
}
