#pragma once

#include <chrono>
#include <optional>
#include <string>

/**
 * @brief Active or expired auth session record.
 *
 * Raw tokens are never stored; only token_hash is persisted.
 */
struct AuthSession {
    std::string id;
    std::string userId;
    std::optional<std::string> clientId;
    std::string tokenHash;
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point expiresAt;
    std::optional<std::chrono::system_clock::time_point> revokedAt;
    std::optional<std::chrono::system_clock::time_point> lastUsedAt;

    bool isActive(std::chrono::system_clock::time_point now) const;
};
