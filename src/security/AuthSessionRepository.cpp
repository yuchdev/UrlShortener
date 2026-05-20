#include "url_shortener/security/AuthSessionRepository.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>

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

} // namespace

AuthSessionRepository::AuthSessionRepository(soci::session& db)
    : db_(db)
{
}

AuthSession AuthSessionRepository::createSession(const std::string& userId,
                                                  const std::string& tokenHash,
                                                  std::chrono::system_clock::time_point expiresAt,
                                                  const std::string* clientId)
{
    const std::string id           = generateId();
    const auto        now          = std::chrono::system_clock::now();
    const long long   nowEpoch     = toEpoch(now);
    const long long   expiresEpoch = toEpoch(expiresAt);

    std::string     clientIdStr;
    soci::indicator clientIdInd = soci::i_null;
    if (clientId) {
        clientIdStr = *clientId;
        clientIdInd = soci::i_ok;
    }

    db_ << "INSERT INTO auth_sessions(id, user_id, client_id, token_hash, created_at, expires_at) "
           "VALUES(:id, :user_id, :client_id, :token_hash, :created_at, :expires_at)",
           soci::use(id),
           soci::use(userId),
           soci::use(clientIdStr, clientIdInd),
           soci::use(tokenHash),
           soci::use(nowEpoch),
           soci::use(expiresEpoch);

    AuthSession s;
    s.id        = id;
    s.userId    = userId;
    s.tokenHash = tokenHash;
    s.createdAt = now;
    s.expiresAt = expiresAt;
    if (clientId) s.clientId = *clientId;
    return s;
}

std::optional<AuthSession> AuthSessionRepository::findByTokenHash(const std::string& tokenHash)
{
    std::string id, userId, clientIdStr, storedTokenHash;
    long long   createdAt = 0, expiresAt = 0, revokedRaw = 0, lastUsedRaw = 0;
    soci::indicator clientIdInd = soci::i_null;
    soci::indicator revokedInd  = soci::i_null;
    soci::indicator lastUsedInd = soci::i_null;

    soci::statement st = (db_.prepare <<
        "SELECT id, user_id, client_id, token_hash, created_at, expires_at, revoked_at, last_used_at "
        "FROM auth_sessions WHERE token_hash = :token_hash",
        soci::use(tokenHash),
        soci::into(id),
        soci::into(userId),
        soci::into(clientIdStr, clientIdInd),
        soci::into(storedTokenHash),
        soci::into(createdAt),
        soci::into(expiresAt),
        soci::into(revokedRaw, revokedInd),
        soci::into(lastUsedRaw, lastUsedInd));

    st.execute();
    if (!st.fetch()) return std::nullopt;

    AuthSession s;
    s.id        = id;
    s.userId    = userId;
    s.tokenHash = storedTokenHash;
    s.createdAt = fromEpoch(createdAt);
    s.expiresAt = fromEpoch(expiresAt);
    if (clientIdInd == soci::i_ok) s.clientId   = clientIdStr;
    if (revokedInd  == soci::i_ok) s.revokedAt  = fromEpoch(revokedRaw);
    if (lastUsedInd == soci::i_ok) s.lastUsedAt = fromEpoch(lastUsedRaw);

    // Update last_used_at if session is currently active
    const auto now = std::chrono::system_clock::now();
    if (s.isActive(now)) {
        const long long nowEpoch = toEpoch(now);
        db_ << "UPDATE auth_sessions SET last_used_at = :now WHERE id = :id",
               soci::use(nowEpoch),
               soci::use(id);
        s.lastUsedAt = now;
    }

    return s;
}

void AuthSessionRepository::revokeByTokenHash(const std::string& tokenHash)
{
    const long long nowEpoch = toEpoch(std::chrono::system_clock::now());
    db_ << "UPDATE auth_sessions SET revoked_at = :now WHERE token_hash = :token_hash",
           soci::use(nowEpoch),
           soci::use(tokenHash);
}

void AuthSessionRepository::revokeAllForUser(const std::string& userId)
{
    const long long nowEpoch = toEpoch(std::chrono::system_clock::now());
    db_ << "UPDATE auth_sessions SET revoked_at = :now "
           "WHERE user_id = :user_id AND revoked_at IS NULL",
           soci::use(nowEpoch),
           soci::use(userId);
}
