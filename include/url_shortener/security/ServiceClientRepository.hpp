#pragma once

#include <optional>
#include <string>
#include <vector>

#include "url_shortener/security/ServiceClient.hpp"

namespace soci { class session; }

/**
 * @brief Persistence layer for service_clients.
 */
class ServiceClientRepository {
public:
    explicit ServiceClientRepository(soci::session& db);

    ServiceClient createClient(const std::string& clientId,
                               const std::vector<std::string>& allowedScopes,
                               const std::string* clientSecretHash = nullptr);

    std::optional<ServiceClient> findByClientId(const std::string& clientId);

    void touchLastUsed(const std::string& clientId);

private:
    soci::session& db_;
};
