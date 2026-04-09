/** @file 07_expiry_logic_test.cpp @brief Unit test: expiry evaluation logic. */
#define BOOST_TEST_MODULE ExpiryLogicTest
#include <boost/test/unit_test.hpp>
#include "../../src/url_shortener.cpp"

BOOST_AUTO_TEST_CASE(expiry_evaluation_logic)
{
    BOOST_TEST(!isExpired(std::nullopt));
    BOOST_TEST(isExpired(std::optional<std::string>{"2000-01-01T00:00:00Z"}));
}
