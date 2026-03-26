/**
 * @file cli_parser_test.cpp
 * @brief Unit tests for CLI parser behavior.
 */
#define BOOST_TEST_MODULE CliParserTest
#include <boost/test/unit_test.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include <url_shortener/cli/cli_parser.h>

// Helper: build argc/argv from a vector of strings.
struct Argv
{
    explicit Argv(std::vector<std::string> args)
        : strings_(std::move(args))
    {
        for (auto& s : strings_) {
            ptrs_.push_back(s.data());
        }
        ptrs_.push_back(nullptr);  // argv[argc] must be null
    }

    int argc() const { return static_cast<int>(ptrs_.size()) - 1; }
    char** argv() { return ptrs_.data(); }

private:
    std::vector<std::string> strings_;  ///< Owns argv string storage.
    std::vector<char*> ptrs_;  ///< Mutable argv pointer list.
};

// ---- Default values ----

BOOST_AUTO_TEST_CASE(defaults_when_no_args)
{
    Argv args({"simple-http"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(!result.help_requested);
    BOOST_TEST(result.config.http_port == 8000);
    BOOST_TEST(result.config.http_enabled == true);
    BOOST_TEST(result.config.http_redirect_to_https == false);
    BOOST_TEST(!result.config.hsts_max_age.has_value());
    BOOST_TEST(result.config.tls.enabled == false);
    BOOST_TEST(result.config.tls.port == 443);
    BOOST_TEST(result.config.shortener_base_domain == "http://localhost:8000");
    BOOST_TEST(result.config.shortener_default_redirect_type == "temporary");
    BOOST_TEST(!result.config.shortener_default_expiry_seconds.has_value());
    BOOST_TEST(result.config.shortener_generated_slug_length == 7u);
    BOOST_TEST(result.config.shortener_allow_private_targets == false);
    BOOST_TEST(result.config.analytics_enabled == true);
    BOOST_TEST(result.config.analytics_queue_capacity == 1024u);
    BOOST_TEST(result.config.analytics_client_hash_salt == "dev-analytics-salt");
    BOOST_TEST(result.config.request_id_max_length == 64u);
    BOOST_TEST(result.config.max_request_body_bytes == 65536u);
    BOOST_TEST(result.config.max_request_target_length == 2048u);
}

// ---- --help ----

BOOST_AUTO_TEST_CASE(help_flag_sets_help_requested)
{
    Argv args({"simple-http", "--help"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.help_requested);
    BOOST_TEST(!result.help_text.empty());
}

BOOST_AUTO_TEST_CASE(help_short_flag)
{
    Argv args({"simple-http", "-h"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.help_requested);
}

// ---- HTTP options ----

BOOST_AUTO_TEST_CASE(explicit_http_port)
{
    Argv args({"simple-http", "--http-port", "9090"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.http_port == 9090);
}

BOOST_AUTO_TEST_CASE(positional_port)
{
    Argv args({"simple-http", "9000"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.http_port == 9000);
}

BOOST_AUTO_TEST_CASE(http_enabled_false)
{
    Argv args({"simple-http", "--http-enabled", "false"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.http_enabled == false);
}

BOOST_AUTO_TEST_CASE(http_redirect_to_https_true)
{
    Argv args({"simple-http", "--http-redirect-to-https", "true"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.http_redirect_to_https == true);
}

BOOST_AUTO_TEST_CASE(hsts_max_age_set)
{
    Argv args({"simple-http", "--hsts-max-age", "300"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_REQUIRE(result.config.hsts_max_age.has_value());
    BOOST_TEST(*result.config.hsts_max_age == 300u);
}

// ---- TLS options ----

BOOST_AUTO_TEST_CASE(tls_enabled_true)
{
    Argv args({"simple-http", "--tls-enabled", "true"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.tls.enabled == true);
}

BOOST_AUTO_TEST_CASE(https_port_explicit)
{
    Argv args({"simple-http", "--https-port", "8443"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.tls.port == 8443);
}

BOOST_AUTO_TEST_CASE(tls_cert_and_key)
{
    Argv args({
        "simple-http",
        "--tls-cert",
        "/path/to/server.crt",
        "--tls-key",
        "/path/to/server.key",
    });
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.tls.certificate_chain_file == "/path/to/server.crt");
    BOOST_TEST(result.config.tls.private_key_file == "/path/to/server.key");
}

BOOST_AUTO_TEST_CASE(tls_key_passphrase)
{
    Argv args({"simple-http", "--tls-key-passphrase", "secret"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_REQUIRE(result.config.tls.private_key_passphrase.has_value());
    BOOST_TEST(*result.config.tls.private_key_passphrase == "secret");
}

BOOST_AUTO_TEST_CASE(tls_ca_file)
{
    Argv args({"simple-http", "--tls-ca-file", "/etc/ssl/ca.crt"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_REQUIRE(result.config.tls.ca_file.has_value());
    BOOST_TEST(*result.config.tls.ca_file == "/etc/ssl/ca.crt");
}

BOOST_AUTO_TEST_CASE(tls_ca_path)
{
    Argv args({"simple-http", "--tls-ca-path", "/etc/ssl/certs"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_REQUIRE(result.config.tls.ca_path.has_value());
    BOOST_TEST(*result.config.tls.ca_path == "/etc/ssl/certs");
}

BOOST_AUTO_TEST_CASE(tls_client_auth_optional)
{
    Argv args({"simple-http", "--tls-client-auth", "optional"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_CHECK(
        result.config.tls.client_auth_mode == TlsConfig::ClientAuthMode::optional
    );
}

BOOST_AUTO_TEST_CASE(tls_client_auth_required)
{
    Argv args({"simple-http", "--tls-client-auth", "required"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_CHECK(
        result.config.tls.client_auth_mode == TlsConfig::ClientAuthMode::required
    );
}

BOOST_AUTO_TEST_CASE(tls_client_auth_none)
{
    Argv args({"simple-http", "--tls-client-auth", "none"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_CHECK(
        result.config.tls.client_auth_mode == TlsConfig::ClientAuthMode::none
    );
}

BOOST_AUTO_TEST_CASE(tls_session_tickets_true)
{
    Argv args({"simple-http", "--tls-session-tickets", "true"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.tls.session_tickets == true);
}

BOOST_AUTO_TEST_CASE(tls_session_cache_false)
{
    Argv args({"simple-http", "--tls-session-cache", "false"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.tls.session_cache == false);
}

// ---- Shortener options ----

BOOST_AUTO_TEST_CASE(shortener_base_domain)
{
    Argv args({"simple-http", "--shortener-base-domain", "https://short.example.com"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.shortener_base_domain == "https://short.example.com");
}

BOOST_AUTO_TEST_CASE(shortener_default_redirect_type_permanent)
{
    Argv args({"simple-http", "--shortener-default-redirect-type", "permanent"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.shortener_default_redirect_type == "permanent");
}

BOOST_AUTO_TEST_CASE(shortener_default_expiry_seconds)
{
    Argv args({"simple-http", "--shortener-default-expiry-seconds", "3600"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_REQUIRE(result.config.shortener_default_expiry_seconds.has_value());
    BOOST_TEST(*result.config.shortener_default_expiry_seconds == 3600u);
}

BOOST_AUTO_TEST_CASE(shortener_generated_slug_length)
{
    Argv args({"simple-http", "--shortener-generated-slug-length", "10"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.shortener_generated_slug_length == 10u);
}

BOOST_AUTO_TEST_CASE(shortener_allow_private_targets_true)
{
    Argv args({"simple-http", "--shortener-allow-private-targets", "true"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.shortener_allow_private_targets == true);
}

// ---- Analytics options ----

BOOST_AUTO_TEST_CASE(analytics_disabled)
{
    Argv args({"simple-http", "--analytics-enabled", "false"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.analytics_enabled == false);
}

BOOST_AUTO_TEST_CASE(analytics_queue_capacity)
{
    Argv args({"simple-http", "--analytics-queue-capacity", "512"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.analytics_queue_capacity == 512u);
}

BOOST_AUTO_TEST_CASE(analytics_client_hash_salt)
{
    Argv args({"simple-http", "--analytics-client-hash-salt", "prod-salt-xyz"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.analytics_client_hash_salt == "prod-salt-xyz");
}

// ---- Request limit options ----

BOOST_AUTO_TEST_CASE(request_id_max_length)
{
    Argv args({"simple-http", "--request-id-max-length", "128"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.request_id_max_length == 128u);
}

BOOST_AUTO_TEST_CASE(max_request_body_bytes)
{
    Argv args({"simple-http", "--max-request-body-bytes", "131072"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.max_request_body_bytes == 131072u);
}

BOOST_AUTO_TEST_CASE(max_request_target_length)
{
    Argv args({"simple-http", "--max-request-target-length", "4096"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.max_request_target_length == 4096u);
}

// ---- Boolean value variants ----

BOOST_AUTO_TEST_CASE(bool_variants_numeric_zero)
{
    Argv args({"simple-http", "--http-enabled", "0"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.http_enabled == false);
}

BOOST_AUTO_TEST_CASE(bool_variants_on)
{
    Argv args({"simple-http", "--http-enabled", "on"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.http_enabled == true);
}

BOOST_AUTO_TEST_CASE(bool_variants_off)
{
    Argv args({"simple-http", "--http-enabled", "off"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.http_enabled == false);
}

BOOST_AUTO_TEST_CASE(bool_variants_yes)
{
    Argv args({"simple-http", "--http-enabled", "yes"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.http_enabled == true);
}

BOOST_AUTO_TEST_CASE(bool_variants_no)
{
    Argv args({"simple-http", "--http-enabled", "no"});
    CliParser parser;
    auto result = parser.parse(args.argc(), args.argv());

    BOOST_TEST(result.config.http_enabled == false);
}

// ---- Invalid input ----

BOOST_AUTO_TEST_CASE(unknown_option_throws)
{
    Argv args({"simple-http", "--unknown-option", "value"});
    CliParser parser;

    BOOST_CHECK_THROW(
        parser.parse(args.argc(), args.argv()), std::invalid_argument
    );
}

BOOST_AUTO_TEST_CASE(invalid_bool_value_throws)
{
    Argv args({"simple-http", "--http-enabled", "maybe"});
    CliParser parser;

    BOOST_CHECK_THROW(
        parser.parse(args.argc(), args.argv()), std::invalid_argument
    );
}

BOOST_AUTO_TEST_CASE(port_zero_throws)
{
    Argv args({"simple-http", "--http-port", "0"});
    CliParser parser;

    BOOST_CHECK_THROW(
        parser.parse(args.argc(), args.argv()), std::out_of_range
    );
}

BOOST_AUTO_TEST_CASE(port_above_max_throws)
{
    Argv args({"simple-http", "--http-port", "65536"});
    CliParser parser;

    BOOST_CHECK_THROW(
        parser.parse(args.argc(), args.argv()), std::out_of_range
    );
}
