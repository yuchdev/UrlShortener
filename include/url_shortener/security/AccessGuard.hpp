#pragma once

#include <stdexcept>
#include <string>

#include "url_shortener/security/ControlSet.hpp"

/**
 * @brief Thrown when an operation is not permitted for the active control set.
 */
class AccessDeniedError : public std::runtime_error {
public:
    explicit AccessDeniedError(const std::string& message);
};

/**
 * @brief Guards an operation by asserting the required permission.
 *
 * Obtain one from an authenticated session or service context.
 * Throws AccessDeniedError if the required permission is not available.
 */
class AccessGuard {
public:
    explicit AccessGuard(ControlSet controlSet);

    void requireRead() const;
    void requireWrite() const;
    void requireMigration() const;
    void requireUserManagement() const;

    ControlSet controlSet() const;

private:
    ControlSet controlSet_;
};
