#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief A local service client registered to call the auth broker.
 *
 * Service clients are identified by `clientId` and optionally
 * authenticated with a hashed secret. `allowedScopes` lists the
 * operations the client may request.
 */
struct ServiceClient
{
    ///< Randomly generated opaque database identifier.
    std::string id;

    ///< Human-readable client identifier used for lookup and audit log
    ///< entries; unique in the `service_clients` table.
    std::string clientId;

    ///< SHA-256 hex-encoded hash of the client secret, if one was set;
    ///< absent for clients that authenticate without a secret. The raw
    ///< secret is never stored.
    std::optional<std::string> clientSecretHash;

    ///< List of scope strings this client is permitted to request.
    std::vector<std::string> allowedScopes;

    ///< Whether the client record is currently active; inactive clients
    ///< may still be returned by `findByClientId`.
    bool isActive {true};

    ///< Wall-clock time the client record was created.
    std::chrono::system_clock::time_point createdAt;

    ///< Wall-clock time the client record was last modified.
    std::chrono::system_clock::time_point updatedAt;

    ///< Wall-clock time `touchLastUsed` was last called for this
    ///< client; absent until the first call.
    std::optional<std::chrono::system_clock::time_point> lastUsedAt;

    /**
     * @brief Returns whether `scope` appears in `allowedScopes`.
     * @param scope The scope string to check; exact match, linear scan.
     * @return `true` if `scope` is found in `allowedScopes`; `false`
     * otherwise.
     */
    bool hasScope(const std::string& scope) const;
};
