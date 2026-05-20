#define BOOST_TEST_MODULE SecurityUserManagementServiceTest
#include <boost/test/unit_test.hpp>

#include <soci/soci.h>
#include <soci/sqlite3/soci-sqlite3.h>

#include "url_shortener/security/AccessGuard.hpp"
#include "url_shortener/security/AuthAuditLogRepository.hpp"
#include "url_shortener/security/PasswordHasher.hpp"
#include "url_shortener/security/UserCredentialsRepository.hpp"
#include "url_shortener/security/UserManagementService.hpp"

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

BOOST_AUTO_TEST_CASE(admin_can_create_user)
{
    soci::session db(soci::sqlite3, ":memory:");
    createSchema(db);

    UserCredentialsRepository users(db);
    PasswordHasher             hasher;
    AuthAuditLogRepository     audit(db);
    UserManagementService      svc(users, hasher, audit);

    AccessGuard adminGuard(ControlSet::Admin);
    auto user = svc.createUser(adminGuard, "reader", "pass", ControlSet::User);
    BOOST_TEST(user.username == "reader");
    BOOST_CHECK(user.controlSet == ControlSet::User);
}

BOOST_AUTO_TEST_CASE(regular_user_cannot_create_user)
{
    soci::session db(soci::sqlite3, ":memory:");
    createSchema(db);

    UserCredentialsRepository users(db);
    PasswordHasher             hasher;
    AuthAuditLogRepository     audit(db);
    UserManagementService      svc(users, hasher, audit);

    AccessGuard userGuard(ControlSet::User);
    BOOST_CHECK_THROW(svc.createUser(userGuard, "reader", "pass", ControlSet::User),
                      AccessDeniedError);
}

BOOST_AUTO_TEST_CASE(admin_can_list_users)
{
    soci::session db(soci::sqlite3, ":memory:");
    createSchema(db);

    UserCredentialsRepository users(db);
    PasswordHasher             hasher;
    AuthAuditLogRepository     audit(db);
    UserManagementService      svc(users, hasher, audit);

    AccessGuard adminGuard(ControlSet::Admin);
    svc.createUser(adminGuard, "alice", "pass", ControlSet::User);
    svc.createUser(adminGuard, "bob",   "pass", ControlSet::Admin);

    auto list = svc.listUsers(adminGuard);
    BOOST_TEST(list.size() == 2U);
}

BOOST_AUTO_TEST_CASE(admin_can_deactivate_user)
{
    soci::session db(soci::sqlite3, ":memory:");
    createSchema(db);

    UserCredentialsRepository users(db);
    PasswordHasher             hasher;
    AuthAuditLogRepository     audit(db);
    UserManagementService      svc(users, hasher, audit);

    AccessGuard adminGuard(ControlSet::Admin);
    svc.createUser(adminGuard, "alice", "pass", ControlSet::User);
    svc.deactivateUser(adminGuard, "alice");

    // findByUsername returns nullopt for inactive users
    auto found = users.findByUsername("alice");
    BOOST_TEST(!found.has_value());
}
