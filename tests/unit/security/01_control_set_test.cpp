#define BOOST_TEST_MODULE SecurityControlSetTest
#include <boost/test/unit_test.hpp>

#include "url_shortener/security/ControlSet.hpp"

BOOST_AUTO_TEST_CASE(admin_has_all_permissions)
{
    BOOST_TEST(canRead(ControlSet::Admin));
    BOOST_TEST(canWrite(ControlSet::Admin));
    BOOST_TEST(canMigrate(ControlSet::Admin));
    BOOST_TEST(canManageUsers(ControlSet::Admin));
}

BOOST_AUTO_TEST_CASE(user_has_read_only_permissions)
{
    BOOST_TEST(canRead(ControlSet::User));
    BOOST_TEST(!canWrite(ControlSet::User));
    BOOST_TEST(!canMigrate(ControlSet::User));
    BOOST_TEST(!canManageUsers(ControlSet::User));
}

BOOST_AUTO_TEST_CASE(to_string_roundtrip)
{
    BOOST_TEST(toString(ControlSet::Admin) == "admin");
    BOOST_TEST(toString(ControlSet::User)  == "user");
    BOOST_CHECK(controlSetFromString("admin") == ControlSet::Admin);
    BOOST_CHECK(controlSetFromString("user")  == ControlSet::User);
}

BOOST_AUTO_TEST_CASE(unknown_control_set_string_is_rejected)
{
    BOOST_CHECK_THROW(controlSetFromString("superuser"), std::invalid_argument);
    BOOST_CHECK_THROW(controlSetFromString(""),          std::invalid_argument);
}
