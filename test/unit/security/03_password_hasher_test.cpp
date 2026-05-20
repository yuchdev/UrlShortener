#define BOOST_TEST_MODULE SecurityPasswordHasherTest
#include <boost/test/unit_test.hpp>

#include "url_shortener/security/PasswordHasher.hpp"

BOOST_AUTO_TEST_CASE(hash_is_non_empty)
{
    PasswordHasher h;
    const auto hash = h.hashPassword("secret");
    BOOST_TEST(!hash.empty());
}

BOOST_AUTO_TEST_CASE(hash_differs_from_plaintext)
{
    PasswordHasher h;
    const std::string pw   = "my-password";
    const std::string hash = h.hashPassword(pw);
    BOOST_TEST(hash != pw);
}

BOOST_AUTO_TEST_CASE(correct_password_verifies)
{
    PasswordHasher h;
    const auto hash = h.hashPassword("correct");
    BOOST_TEST(h.verifyPassword("correct", hash));
}

BOOST_AUTO_TEST_CASE(incorrect_password_does_not_verify)
{
    PasswordHasher h;
    const auto hash = h.hashPassword("correct");
    BOOST_TEST(!h.verifyPassword("wrong", hash));
}

BOOST_AUTO_TEST_CASE(two_hashes_of_same_password_are_different)
{
    PasswordHasher h;
    const auto h1 = h.hashPassword("same");
    const auto h2 = h.hashPassword("same");
    // Different salts produce different hashes
    BOOST_TEST(h1 != h2);
    BOOST_TEST(h.verifyPassword("same", h1));
    BOOST_TEST(h.verifyPassword("same", h2));
}
