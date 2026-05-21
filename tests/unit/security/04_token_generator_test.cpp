#define BOOST_TEST_MODULE SecurityTokenGeneratorTest
#include <boost/test/unit_test.hpp>

#include "url_shortener/security/TokenGenerator.hpp"

BOOST_AUTO_TEST_CASE(token_is_non_empty)
{
    TokenGenerator g;
    const auto tok = g.generateToken();
    BOOST_TEST(!tok.empty());
}

BOOST_AUTO_TEST_CASE(token_has_correct_length)
{
    TokenGenerator g;
    // 32 bytes => 64 hex chars
    BOOST_TEST(g.generateToken(32).size() == 64U);
    BOOST_TEST(g.generateToken(16).size() == 32U);
}

BOOST_AUTO_TEST_CASE(tokens_are_unique)
{
    TokenGenerator g;
    BOOST_TEST(g.generateToken() != g.generateToken());
}

BOOST_AUTO_TEST_CASE(token_hash_is_non_empty)
{
    TokenGenerator g;
    const auto raw  = g.generateToken();
    const auto hash = g.hashToken(raw);
    BOOST_TEST(!hash.empty());
    BOOST_TEST(hash != raw);
}

BOOST_AUTO_TEST_CASE(same_raw_token_produces_same_hash)
{
    TokenGenerator g;
    const auto raw = g.generateToken();
    BOOST_TEST(g.hashToken(raw) == g.hashToken(raw));
}
