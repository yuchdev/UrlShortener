#include "url_shortener/security/ServiceClientRepository.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>

#include <openssl/rand.h>
#include <soci/soci.h>

namespace {

long long toEpoch(std::chrono::system_clock::time_point tp)
{
    return std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
}

std::chrono::system_clock::time_point fromEpoch(long long epoch)
{
    return std::chrono::system_clock::time_point(std::chrono::seconds(epoch));
}

std::string generateId()
{
    unsigned char bytes[16];
    RAND_bytes(bytes, 16);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        oss << std::setw(2) << static_cast<unsigned>(bytes[i]);
    }
    return oss.str();
}

std::string joinScopes(const std::vector<std::string>& scopes)
{
    std::string out;
    for (std::size_t i = 0; i < scopes.size(); ++i) {
        if (i) out += ',';
        out += scopes[i];
    }
    return out;
}

std::vector<std::string> splitScopes(const std::string& s)
{
    std::vector<std::string> out;
    std::string tok;
    for (char c : s) {
        if (c == ',') {
            if (!tok.empty()) { out.push_back(tok); tok.clear(); }
        } else {
            tok += c;
        }
    }
    if (!tok.empty()) out.push_back(tok);
    return out;
}

} // namespace

ServiceClientRepository::ServiceClientRepository(soci::session& db)
    : db_(db)
{
}

ServiceClient ServiceClientRepository::createClient(const std::string& clientId,
                                                     const std::vector<std::string>& allowedScopes,
                                                     const std::string* clientSecretHash)
{
    const std::string id        = generateId();
    const std::string scopesStr = joinScopes(allowedScopes);
    const auto        now       = std::chrono::system_clock::now();
    const long long   nowEpoch  = toEpoch(now);

    std::string     secretHashStr;
    soci::indicator secretHashInd = soci::i_null;
    if (clientSecretHash) {
        secretHashStr = *clientSecretHash;
        secretHashInd = soci::i_ok;
    }

    db_ << "INSERT INTO service_clients(id, client_id, client_secret_hash, allowed_scopes, "
           "is_active, created_at, updated_at) "
           "VALUES(:id, :client_id, :client_secret_hash, :allowed_scopes, 1, :created_at, :updated_at)",
           soci::use(id),
           soci::use(clientId),
           soci::use(secretHashStr, secretHashInd),
           soci::use(scopesStr),
           soci::use(nowEpoch),
           soci::use(nowEpoch);

    ServiceClient c;
    c.id            = id;
    c.clientId      = clientId;
    c.allowedScopes = allowedScopes;
    c.isActive      = true;
    c.createdAt     = now;
    c.updatedAt     = now;
    if (clientSecretHash) c.clientSecretHash = *clientSecretHash;
    return c;
}

std::optional<ServiceClient> ServiceClientRepository::findByClientId(const std::string& clientId)
{
    std::string id, secretHashStr, scopesStr;
    int         isActive  = 0;
    long long   createdAt = 0, updatedAt = 0, lastUsedRaw = 0;
    soci::indicator secretHashInd = soci::i_null;
    soci::indicator lastUsedInd   = soci::i_null;

    soci::statement st = (db_.prepare <<
        "SELECT id, client_secret_hash, allowed_scopes, is_active, created_at, updated_at, last_used_at "
        "FROM service_clients WHERE client_id = :client_id",
        soci::use(clientId),
        soci::into(id),
        soci::into(secretHashStr, secretHashInd),
        soci::into(scopesStr),
        soci::into(isActive),
        soci::into(createdAt),
        soci::into(updatedAt),
        soci::into(lastUsedRaw, lastUsedInd));

    st.execute();
    if (!st.fetch()) return std::nullopt;

    ServiceClient c;
    c.id            = id;
    c.clientId      = clientId;
    c.allowedScopes = splitScopes(scopesStr);
    c.isActive      = (isActive != 0);
    c.createdAt     = fromEpoch(createdAt);
    c.updatedAt     = fromEpoch(updatedAt);
    if (secretHashInd == soci::i_ok) c.clientSecretHash = secretHashStr;
    if (lastUsedInd   == soci::i_ok) c.lastUsedAt = fromEpoch(lastUsedRaw);
    return c;
}

void ServiceClientRepository::touchLastUsed(const std::string& clientId)
{
    const long long nowEpoch = toEpoch(std::chrono::system_clock::now());
    db_ << "UPDATE service_clients SET last_used_at = :now WHERE client_id = :client_id",
           soci::use(nowEpoch),
           soci::use(clientId);
}
