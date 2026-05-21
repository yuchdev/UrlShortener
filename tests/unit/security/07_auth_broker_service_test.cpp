#define BOOST_TEST_MODULE SecurityAuthBrokerServiceTest
#include <boost/test/unit_test.hpp>

#include <soci/soci.h>
#include <soci/sqlite3/soci-sqlite3.h>

#include "url_shortener/security/AuthAuditLogRepository.hpp"
#include "url_shortener/security/AuthBrokerService.hpp"
#include "url_shortener/security/AuthSessionRepository.hpp"
#include "url_shortener/security/PasswordHasher.hpp"
#include "url_shortener/security/TokenGenerator.hpp"
#include "url_shortener/security/UserCredentialsRepository.hpp"

static void createSchema(soci::session& db)
{
    db << "CREATE TABLE IF NOT EXISTS app_users ("
          "id TEXT PRIMARY KEY, username TEXT NOT NULL UNIQUE, password_hash TEXT NOT NULL,"
          "control_set TEXT NOT NULL, is_active INTEGER NOT NULL DEFAULT 1,"
          "created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL, last_login_at INTEGER NULL"
          ")";
    db << "CREATE TABLE IF NOT EXISTS auth_sessions ("
          "id TEXT PRIMARY KEY, user_id TEXT NOT NULL, client_id TEXT NULL,"
          "token_hash TEXT NOT NULL UNIQUE,"
          "created_at INTEGER NOT NULL, expires_at INTEGER NOT NULL,"
          "revoked_at INTEGER NULL, last_used_at INTEGER NULL"
          ")";
    db << "CREATE TABLE IF NOT EXISTS auth_audit_log ("
          "id TEXT PRIMARY KEY, client_id TEXT NULL, username TEXT NULL, user_id TEXT NULL,"
          "action TEXT NOT NULL, result TEXT NOT NULL, reason TEXT NULL, created_at INTEGER NOT NULL"
          ")";
}

struct Fixture {
    soci::session             db;
    UserCredentialsRepository users;
    AuthSessionRepository     sessions;
    PasswordHasher            hasher;
    TokenGenerator            tokenGen;
    AuthAuditLogRepository    audit;
    AuthBrokerService         broker;

    Fixture()
        : db(soci::sqlite3, ":memory:")
        , users(db)
        , sessions(db)
        , audit(db)
        , broker(users, sessions, hasher, tokenGen, audit, std::chrono::hours(1))
    {
        createSchema(db);
        const auto hash = hasher.hashPassword("secret");
        users.createUser("testuser", hash, ControlSet::User);
    }
};

BOOST_FIXTURE_TEST_CASE(correct_credentials_verify, Fixture)
{
    auto result = broker.verifyPassword("testuser", "secret");
    BOOST_TEST(result.ok);
    BOOST_TEST(result.user.has_value());
    BOOST_TEST(result.user->username == "testuser");
}

BOOST_FIXTURE_TEST_CASE(wrong_password_does_not_verify, Fixture)
{
    auto result = broker.verifyPassword("testuser", "wrong");
    BOOST_TEST(!result.ok);
}

BOOST_FIXTURE_TEST_CASE(unknown_user_does_not_verify, Fixture)
{
    auto result = broker.verifyPassword("nobody", "secret");
    BOOST_TEST(!result.ok);
}

BOOST_FIXTURE_TEST_CASE(session_created_with_correct_credentials, Fixture)
{
    auto result = broker.createSession("testuser", "secret");
    BOOST_TEST(result.ok);
    BOOST_TEST(!result.rawToken.empty());
    BOOST_TEST(result.session.has_value());
}

BOOST_FIXTURE_TEST_CASE(token_introspects_as_active, Fixture)
{
    auto created = broker.createSession("testuser", "secret");
    BOOST_REQUIRE(created.ok);

    auto introspect = broker.introspectToken(created.rawToken);
    BOOST_TEST(introspect.active);
}

BOOST_FIXTURE_TEST_CASE(revoked_token_introspects_as_inactive, Fixture)
{
    auto created = broker.createSession("testuser", "secret");
    BOOST_REQUIRE(created.ok);

    broker.revokeToken(created.rawToken);
    auto introspect = broker.introspectToken(created.rawToken);
    BOOST_TEST(!introspect.active);
}

BOOST_FIXTURE_TEST_CASE(raw_token_is_not_stored_in_sessions, Fixture)
{
    auto created = broker.createSession("testuser", "secret");
    BOOST_REQUIRE(created.ok);

    // The stored token_hash must not equal the raw token
    BOOST_TEST(created.session->tokenHash != created.rawToken);
}
