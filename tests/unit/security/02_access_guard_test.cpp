#define BOOST_TEST_MODULE SecurityAccessGuardTest
#include <boost/test/unit_test.hpp>

#include "url_shortener/security/AccessGuard.hpp"

BOOST_AUTO_TEST_CASE(admin_guard_allows_all_operations)
{
    AccessGuard g(ControlSet::Admin);
    BOOST_CHECK_NO_THROW(g.requireRead());
    BOOST_CHECK_NO_THROW(g.requireWrite());
    BOOST_CHECK_NO_THROW(g.requireMigration());
    BOOST_CHECK_NO_THROW(g.requireUserManagement());
}

BOOST_AUTO_TEST_CASE(user_guard_allows_read_only)
{
    AccessGuard g(ControlSet::User);
    BOOST_CHECK_NO_THROW(g.requireRead());
    BOOST_CHECK_THROW(g.requireWrite(),          AccessDeniedError);
    BOOST_CHECK_THROW(g.requireMigration(),      AccessDeniedError);
    BOOST_CHECK_THROW(g.requireUserManagement(), AccessDeniedError);
}

BOOST_AUTO_TEST_CASE(access_denied_error_is_runtime_error)
{
    try {
        AccessGuard g(ControlSet::User);
        g.requireWrite();
        BOOST_FAIL("Expected exception");
    } catch (const AccessDeniedError& e) {
        const std::string msg = e.what();
        BOOST_TEST(!msg.empty());
    }
}
