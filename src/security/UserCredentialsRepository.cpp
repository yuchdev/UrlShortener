#include "url_shortener/security/UserCredentialsRepository.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <openssl/rand.h>
#include <soci/soci.h>

#include "url_shortener/security/ControlSet.hpp"

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

UserCredentials buildUserRow(const std::string& id,
                              const std::string& username,
                              const std::string& passwordHash,
                              const std::string& controlSetStr,
                              int                isActive,
                              long long          createdAt,
                              long long          updatedAt,
                              soci::indicator    lastLoginInd,
                              long long          lastLoginRaw)
{
    UserCredentials u;
    u.id           = id;
    u.username     = username;
    u.passwordHash = passwordHash;
    u.controlSet   = controlSetFromString(controlSetStr);
    u.isActive     = (isActive != 0);
    u.createdAt    = fromEpoch(createdAt);
    u.updatedAt    = fromEpoch(updatedAt);
    if (lastLoginInd == soci::i_ok) {
        u.lastLoginAt = fromEpoch(lastLoginRaw);
    }
    return u;
}

} // namespace

UserCredentialsRepository::UserCredentialsRepository(soci::session& db)
    : db_(db)
{
}

bool UserCredentialsRepository::activeAdminExists()
{
    int count = 0;
    db_ << "SELECT COUNT(1) FROM app_users WHERE control_set = 'admin' AND is_active = 1",
           soci::into(count);
    return count > 0;
}

UserCredentials UserCredentialsRepository::createUser(const std::string& username,
                                                       const std::string& passwordHash,
                                                       ControlSet          controlSet)
{
    const std::string id            = generateId();
    const std::string controlSetStr = toString(controlSet);
    const auto        now           = std::chrono::system_clock::now();
    const long long   nowEpoch      = toEpoch(now);

    db_ << "INSERT INTO app_users(id, username, password_hash, control_set, is_active, created_at, updated_at) "
           "VALUES(:id, :username, :password_hash, :control_set, 1, :created_at, :updated_at)",
           soci::use(id),
           soci::use(username),
           soci::use(passwordHash),
           soci::use(controlSetStr),
           soci::use(nowEpoch),
           soci::use(nowEpoch);

    UserCredentials u;
    u.id           = id;
    u.username     = username;
    u.passwordHash = passwordHash;
    u.controlSet   = controlSet;
    u.isActive     = true;
    u.createdAt    = now;
    u.updatedAt    = now;
    return u;
}

std::optional<UserCredentials> UserCredentialsRepository::findByUsername(const std::string& username)
{
    std::string id, pwHash, controlSetStr;
    int         isActive  = 0;
    long long   createdAt = 0, updatedAt = 0, lastLoginRaw = 0;
    soci::indicator lastLoginInd = soci::i_null;

    soci::statement st = (db_.prepare <<
        "SELECT id, password_hash, control_set, is_active, created_at, updated_at, last_login_at "
        "FROM app_users WHERE username = :username AND is_active = 1",
        soci::use(username),
        soci::into(id),
        soci::into(pwHash),
        soci::into(controlSetStr),
        soci::into(isActive),
        soci::into(createdAt),
        soci::into(updatedAt),
        soci::into(lastLoginRaw, lastLoginInd));

    st.execute();
    if (!st.fetch()) {
        return std::nullopt;
    }

    return buildUserRow(id, username, pwHash, controlSetStr,
                        isActive, createdAt, updatedAt, lastLoginInd, lastLoginRaw);
}

std::optional<UserCredentials> UserCredentialsRepository::findById(const std::string& userId)
{
    std::string username, pwHash, controlSetStr;
    int         isActive  = 0;
    long long   createdAt = 0, updatedAt = 0, lastLoginRaw = 0;
    soci::indicator lastLoginInd = soci::i_null;

    soci::statement st = (db_.prepare <<
        "SELECT username, password_hash, control_set, is_active, created_at, updated_at, last_login_at "
        "FROM app_users WHERE id = :id AND is_active = 1",
        soci::use(userId),
        soci::into(username),
        soci::into(pwHash),
        soci::into(controlSetStr),
        soci::into(isActive),
        soci::into(createdAt),
        soci::into(updatedAt),
        soci::into(lastLoginRaw, lastLoginInd));

    st.execute();
    if (!st.fetch()) {
        return std::nullopt;
    }

    return buildUserRow(userId, username, pwHash, controlSetStr,
                        isActive, createdAt, updatedAt, lastLoginInd, lastLoginRaw);
}

std::vector<UserCredentials> UserCredentialsRepository::listUsers()
{
    std::vector<UserCredentials> result;

    std::string id, username, pwHash, controlSetStr;
    int         isActive  = 0;
    long long   createdAt = 0, updatedAt = 0, lastLoginRaw = 0;
    soci::indicator lastLoginInd = soci::i_null;

    soci::statement st = (db_.prepare <<
        "SELECT id, username, password_hash, control_set, is_active, created_at, updated_at, last_login_at "
        "FROM app_users ORDER BY created_at",
        soci::into(id),
        soci::into(username),
        soci::into(pwHash),
        soci::into(controlSetStr),
        soci::into(isActive),
        soci::into(createdAt),
        soci::into(updatedAt),
        soci::into(lastLoginRaw, lastLoginInd));

    st.execute();
    while (st.fetch()) {
        result.push_back(buildUserRow(id, username, pwHash, controlSetStr,
                                      isActive, createdAt, updatedAt, lastLoginInd, lastLoginRaw));
    }
    return result;
}

void UserCredentialsRepository::deactivateUser(const std::string& username)
{
    const long long nowEpoch = toEpoch(std::chrono::system_clock::now());
    db_ << "UPDATE app_users SET is_active = 0, updated_at = :updated_at "
           "WHERE username = :username",
           soci::use(nowEpoch),
           soci::use(username);
}

void UserCredentialsRepository::touchLastLogin(const std::string& userId)
{
    const long long nowEpoch = toEpoch(std::chrono::system_clock::now());
    db_ << "UPDATE app_users SET last_login_at = :last_login_at "
           "WHERE id = :id",
           soci::use(nowEpoch),
           soci::use(userId);
}
