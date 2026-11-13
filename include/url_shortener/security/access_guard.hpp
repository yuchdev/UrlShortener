#pragma once

#include <stdexcept>
#include <string>

#include "url_shortener/security/control_set.hpp"

/**
 * @brief Thrown when an operation is not permitted for the active
 * control set.
 */
class AccessDeniedError : public std::runtime_error
{
public:
    /**
     * @brief Constructs the error with a human-readable description.
     * @param message Detail explaining which permission was missing and
     * for which control set.
     */
    explicit AccessDeniedError(const std::string& message);
};

/**
 * @brief Guards an operation by asserting the required permission for
 * the active `ControlSet`.
 *
 * Obtain one from an authenticated session or service context. Each
 * `require*` method throws `AccessDeniedError` if the stored control
 * set does not hold that permission.
 */
class AccessGuard
{
public:
    /**
     * @brief Constructs a guard that enforces the given control set.
     * @param controlSet Role whose capabilities are enforced; stored
     * by value.
     */
    explicit AccessGuard(ControlSet controlSet);

    /**
     * @brief Asserts that the control set permits read operations.
     * @throws AccessDeniedError if `canRead(controlSet_)` returns
     * `false`. Note: the current implementation grants read to all
     * control sets, so this never throws in practice.
     */
    void requireRead() const;

    /**
     * @brief Asserts that the control set permits write (mutating)
     * operations.
     * @throws AccessDeniedError if the control set is not `Admin`.
     */
    void requireWrite() const;

    /**
     * @brief Asserts that the control set permits database migration
     * operations.
     * @throws AccessDeniedError if the control set is not `Admin`.
     */
    void requireMigration() const;

    /**
     * @brief Asserts that the control set permits user management
     * operations.
     * @throws AccessDeniedError if the control set is not `Admin`.
     */
    void requireUserManagement() const;

    /**
     * @brief Returns the control set this guard was constructed with.
     * @return The active `ControlSet`.
     */
    ControlSet controlSet() const;

private:
    ///< Role checked by every `require*` call.
    ControlSet controlSet_;
};
