#define BOOST_TEST_MODULE 03_startup_logging_redaction
#include <boost/test/unit_test.hpp>
#include "url_shortener/observability/StorageObservability.hpp"

BOOST_AUTO_TEST_CASE(password_like_values_are_redacted) {
    BOOST_TEST(observability::redactSecretValue("password", "p@ss") == "[REDACTED]");
}

BOOST_AUTO_TEST_CASE(dsn_credential_is_redacted_but_backend_visible) {
    const auto redacted = observability::redactSecretValue("postgres_dsn", "postgres://user:pass@db:5432/url_shortener");
    BOOST_TEST(redacted.find("postgres://") == 0U);
    BOOST_TEST(redacted.find("[REDACTED]") != std::string::npos);
    BOOST_TEST(redacted.find("pass") == std::string::npos);
}
