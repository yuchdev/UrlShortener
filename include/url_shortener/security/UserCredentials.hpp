#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "url_shortener/security/ControlSet.hpp"

/**
 * @brief Persisted user credential record.
 *
 * passwordHash must never be returned through CLI or external API.
 */
struct UserCredentials {
    std::string id;
    std::string username;
    std::string passwordHash;
    ControlSet  controlSet{ControlSet::User};
    bool        isActive{true};
    std::chrono::system_clock::time_point                createdAt;
    std::chrono::system_clock::time_point                updatedAt;
    std::optional<std::chrono::system_clock::time_point> lastLoginAt;
};
