#include "url_shortener/security/AuthAuditLogRepository.hpp"

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

AuthAuditLogRepository::AuthAuditLogRepository(soci::session& db)
    : db_(db)
{
}

void AuthAuditLogRepository::append(const std::string& action,
                                     const std::string& result,
                                     const std::string* username,
                                     const std::string* userId,
                                     const std::string* clientId,
                                     const std::string* reason)
{
    const std::string id       = generateId();
    const long long   nowEpoch = toEpoch(std::chrono::system_clock::now());

    std::string     usernameStr, userIdStr, clientIdStr, reasonStr;
    soci::indicator usernameInd = soci::i_null;
    soci::indicator userIdInd   = soci::i_null;
    soci::indicator clientIdInd = soci::i_null;
    soci::indicator reasonInd   = soci::i_null;

    if (username) { usernameStr = *username; usernameInd = soci::i_ok; }
    if (userId)   { userIdStr   = *userId;   userIdInd   = soci::i_ok; }
    if (clientId) { clientIdStr = *clientId; clientIdInd = soci::i_ok; }
    if (reason)   { reasonStr   = *reason;   reasonInd   = soci::i_ok; }

    db_ << "INSERT INTO auth_audit_log(id, client_id, username, user_id, action, result, reason, created_at) "
           "VALUES(:id, :client_id, :username, :user_id, :action, :result, :reason, :created_at)",
           soci::use(id),
           soci::use(clientIdStr, clientIdInd),
           soci::use(usernameStr, usernameInd),
           soci::use(userIdStr,   userIdInd),
           soci::use(action),
           soci::use(result),
           soci::use(reasonStr, reasonInd),
           soci::use(nowEpoch);
}
