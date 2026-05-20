#define BOOST_TEST_MODULE SecurityFirstAdminBootstrapTest
#include <boost/test/unit_test.hpp>

#include <soci/soci.h>
#include <soci/sqlite3/soci-sqlite3.h>

#include "url_shortener/security/AuthAuditLogRepository.hpp"
#include "url_shortener/security/FirstAdminBootstrap.hpp"
#include "url_shortener/security/PasswordHasher.hpp"
#include "url_shortener/security/UserCredentialsRepository.hpp"

static void createSchema(soci::session& db)
{
    db << "CREATE TABLE IF NOT EXISTS app_users ("
          "id TEXT PRIMARY KEY, username TEXT NOT NULL UNIQUE, password_hash TEXT NOT NULL,"
          "control_set TEXT NOT NULL CHECK(control_set IN('admin','user')),"
          "is_active INTEGER NOT NULL DEFAULT 1,"
          "created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL, last_login_at INTEGER NULL"
          ")";
    db << "CREATE TABLE IF NOT EXISTS auth_audit_log ("
          "id TEXT PRIMARY KEY, client_id TEXT NULL, username TEXT NULL, user_id TEXT NULL,"
          "action TEXT NOT NULL, result TEXT NOT NULL, reason TEXT NULL, created_at INTEGER NOT NULL"
          ")";
}

BOOST_AUTO_TEST_CASE(first_admin_can_be_created_when_no_admin_exists)
{
    soci::session db(soci::sqlite3, ":memory:");
    createSchema(db);

    UserCredentialsRepository users(db);
    PasswordHasher             hasher;
    AuthAuditLogRepository     audit(db);

    FirstAdminBootstrap bootstrap(users, hasher, audit);
    auto user = bootstrap.createFirstAdmin("admin", "secret-password");

    BOOST_TEST(user.username == "admin");
    BOOST_CHECK(user.controlSet == ControlSet::Admin);
    BOOST_TEST(user.isActive);
}

BOOST_AUTO_TEST_CASE(first_admin_cannot_be_created_twice)
{
    soci::session db(soci::sqlite3, ":memory:");
    createSchema(db);

    UserCredentialsRepository users(db);
    PasswordHasher             hasher;
    AuthAuditLogRepository     audit(db);

    FirstAdminBootstrap bootstrap(users, hasher, audit);
    bootstrap.createFirstAdmin("admin", "secret-password");

    BOOST_CHECK_THROW(bootstrap.createFirstAdmin("admin2", "other-password"),
                      AdminAlreadyExistsError);
}

BOOST_AUTO_TEST_CASE(first_admin_password_hash_is_not_plaintext)
{
    soci::session db(soci::sqlite3, ":memory:");
    createSchema(db);

    UserCredentialsRepository users(db);
    PasswordHasher             hasher;
    AuthAuditLogRepository     audit(db);

    FirstAdminBootstrap bootstrap(users, hasher, audit);
    auto user = bootstrap.createFirstAdmin("admin", "my-secret");

    BOOST_TEST(user.passwordHash != "my-secret");
    BOOST_TEST(!user.passwordHash.empty());
}
