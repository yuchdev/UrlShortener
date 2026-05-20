#include "url_shortener/security/AccessGuard.hpp"

AccessDeniedError::AccessDeniedError(const std::string& message)
    : std::runtime_error(message)
{
}

AccessGuard::AccessGuard(ControlSet controlSet)
    : controlSet_(controlSet)
{
}

ControlSet AccessGuard::controlSet() const
{
    return controlSet_;
}

void AccessGuard::requireRead() const
{
    if (!canRead(controlSet_)) {
        throw AccessDeniedError("Read access denied for control set: " + toString(controlSet_));
    }
}

void AccessGuard::requireWrite() const
{
    if (!canWrite(controlSet_)) {
        throw AccessDeniedError("Write access denied for control set: " + toString(controlSet_));
    }
}

void AccessGuard::requireMigration() const
{
    if (!canMigrate(controlSet_)) {
        throw AccessDeniedError("Migration access denied for control set: " + toString(controlSet_));
    }
}

void AccessGuard::requireUserManagement() const
{
    if (!canManageUsers(controlSet_)) {
        throw AccessDeniedError("User management access denied for control set: " + toString(controlSet_));
    }
}
