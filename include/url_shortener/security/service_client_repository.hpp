#pragma once

#include <optional>
#include <string>
#include <vector>

#include "url_shortener/security/service_client.hpp"

namespace soci
{
class session;
}

/**
 * @brief Persistence layer for `service_clients`.
 *
 * Lifetime: does not own `db`; the caller must keep the SOCI session
 * alive for at least as long as this object exists.
 */
class ServiceClientRepository
{
public:
    /**
     * @brief Stores a reference to the SOCI session used for all
     * subsequent queries.
     * @param db Open SOCI session to the application database; must
     * outlive this object.
     */
    explicit ServiceClientRepository(soci::session& db);

    /**
     * @brief Inserts a new service client record and returns the
     * populated `ServiceClient`.
     *
     * Assigns a randomly generated hex ID; `is_active` defaults to
     * `true`.
     * @param clientId Unique human-readable client identifier.
     * @param allowedScopes Permitted scope strings; stored as a
     * comma-separated string internally and returned as a vector.
     * @param clientSecretHash Pre-computed hash of the client secret to
     * store; pass `nullptr` for secret-less clients.
     * @return The newly created `ServiceClient` with all fields
     * populated.
     */
    ServiceClient createClient(const std::string& clientId,
                               const std::vector<std::string>& allowedScopes,
                               const std::string* clientSecretHash = nullptr);

    /**
     * @brief Looks up a service client by its `clientId`.
     * @param clientId The client identifier to search for.
     * @return The matching `ServiceClient` if found (including inactive
     * clients); `std::nullopt` if no record with that `clientId`
     * exists.
     */
    std::optional<ServiceClient> findByClientId(const std::string& clientId);

    /**
     * @brief Updates `last_used_at` to the current time for the client
     * identified by `clientId`.
     *
     * A no-op at the database level if no matching client exists.
     * @param clientId The client identifier to update.
     */
    void touchLastUsed(const std::string& clientId);

private:
    ///< SOCI session used for all DML statements. Not owned; must
    ///< outlive this object.
    soci::session& db_;
};
