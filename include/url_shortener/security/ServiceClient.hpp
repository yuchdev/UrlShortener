#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief A local service client allowed to call the auth broker.
 */
struct ServiceClient {
    std::string id;
    std::string clientId;
    std::optional<std::string> clientSecretHash;
    std::vector<std::string> allowedScopes;
    bool isActive{true};
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point updatedAt;
    std::optional<std::chrono::system_clock::time_point> lastUsedAt;

    bool hasScope(const std::string& scope) const;
};
