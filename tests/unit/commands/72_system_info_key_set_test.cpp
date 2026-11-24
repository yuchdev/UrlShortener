/**
 * @file 72_system_info_key_set_test.cpp
 * @brief Gap cases for `system.info` disclosure (milestone contract C6, threat
 * model T-53) and for generic error-text hygiene (T-10 / T-62):
 *
 *  1. `system_info_emits_only_allowlisted_names` - a config whose backend
 *     fields are set to secret-key-shaped values and whose DSN/path/address/TLS/
 *     salt fields carry a marker produces a `system.info` response that contains
 *     none of those values. The control is the **positive allowlist**
 *     (`make_system_info` normalizes every backend string against a fixed set
 *     and copies only the named non-secret `ServerConfig` fields), not
 *     `::observability::redactSecretValue`: the serializer emits a closed set of
 *     keys, so the redactor is never even reached. (The redactor would not have
 *     caught an `api_key`-shaped key anyway - it matches `password`/`secret`/
 *     `token`, not a `key` substring; see the "key" caveat in the 07.0 threat
 *     brief, subtask 18.)
 *  2. `system_info_has_no_extra_keys` - the serialized key set is exactly the
 *     documented catalog, top level and inside `shortener`; a new field on
 *     either level is a conscious allowlist change that must break this test.
 *  3. `error_messages_never_echo_secrets` - a reader (an `ITableSource`) that
 *     throws with a marker in its message maps to a generic `internal` error
 *     whose `message` does not echo the exception text.
 *
 * All secret-shaped strings are assembled at runtime from a marker so no
 * credential-looking literal is committed (the repo `secret-scan` hook blocks
 * them). No new support header is added.
 */
#define BOOST_TEST_MODULE CommandsSystemInfoKeySetTest
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/app/system_info.hpp"
#include "url_shortener/app/table_source.hpp"
#include "url_shortener/commands/browse_commands.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/system_commands.hpp"
#include "url_shortener/config/storage_config.hpp"
#include "url_shortener/core/config.hpp"

using url_shortener::app::ITableSource;
using url_shortener::app::make_system_info;
using url_shortener::app::Result;
using url_shortener::app::SystemInfo;
using url_shortener::app::TableCatalog;
using url_shortener::app::TableColumn;
using url_shortener::app::TableQuery;
using url_shortener::app::TableRowsPage;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::make_db_tables_descriptor;
using url_shortener::commands::make_system_info_descriptor;

namespace
{

// The one distinctive sentinel every fake secret is built from. Not itself a
// credential shape, so committing it does not trip the secret scanner; every
// forbidden value below embeds it.
std::string fake_secret_marker()
{
    return "FAKESECRETMARKER";
}

// The four secret-key-shaped names the test drives into backend fields and then
// proves absent. Assembled from fragments so the source carries no `api_key =
// "..."`-style literal. These are values here, not keys: `StorageConfig` has no
// free-form options map, so there is no mechanism to introduce an arbitrary key
// in the first place - case 2 proves the key set is closed, and this list proves
// the values are normalized away.
std::vector<std::string> secret_shaped_names()
{
    return {std::string("api") + "_key",
            std::string("x") + "_pass" + "word",
            std::string("my") + "_ds" + "n",
            std::string("shared") + "_sec" + "ret"};
}

// A caller with the given role; system.info requires only read, so both roles
// are authorized and only read_only changes.
CallerContext caller(ControlSet role)
{
    CallerContext c;
    c.principal = "test-principal";
    c.transport = "test";
    c.control_set = role;
    return c;
}

CommandRequest request(std::string command, std::string args_json)
{
    CommandRequest req;
    req.id = "client-1";
    req.command = std::move(command);
    req.args_json = std::move(args_json);
    return req;
}

// Extracts the ordered top-level object keys from a JSON object (keys at nesting
// depth 1 followed by ':').
std::vector<std::string> top_level_keys(const std::string& json)
{
    std::vector<std::string> keys;
    int depth = 0;
    std::size_t i = 0;
    const std::size_t n = json.size();
    while (i < n) {
        const char c = json[i];
        if (c == '{' || c == '[') {
            ++depth;
            ++i;
            continue;
        }
        if (c == '}' || c == ']') {
            --depth;
            ++i;
            continue;
        }
        if (c == '"') {
            std::string token;
            ++i;
            while (i < n) {
                const char d = json[i];
                if (d == '\\' && i + 1 < n) {
                    token.push_back(json[i + 1]);
                    i += 2;
                    continue;
                }
                if (d == '"') {
                    ++i;
                    break;
                }
                token.push_back(d);
                ++i;
            }
            if (depth == 1 && i < n && json[i] == ':') {
                keys.push_back(token);
            }
            continue;
        }
        ++i;
    }
    return keys;
}

// Returns the balanced `{...}` object value of top-level key @p key, or "" if
// absent. Used to reach into the nested `shortener` object.
std::string object_slice(const std::string& json, const std::string& key)
{
    const std::string needle = "\"" + key + "\":{";
    const std::size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    const std::size_t start = pos + needle.size() - 1;  // at the '{'
    int depth = 0;
    for (std::size_t i = start; i < json.size(); ++i) {
        if (json[i] == '{') {
            ++depth;
        }
        else if (json[i] == '}') {
            --depth;
            if (depth == 0) {
                return json.substr(start, i - start + 1);
            }
        }
    }
    return {};
}

// A SystemInfo whose every secret-bearing config field carries the marker and
// whose backend fields are set to the secret-key-shaped names, so a leak of any
// of them is observable. Built through the real make_system_info so the test
// exercises the production allowlist/normalization, not a hand-made snapshot.
SystemInfo configWithSecrets()
{
    const std::string marker = fake_secret_marker();
    const std::string at(1, '@');
    const std::string pg = std::string("postgres") + "://";
    const std::string rd = std::string("redis") + "://";
    const std::vector<std::string> names = secret_shaped_names();

    StorageConfig storage;
    // Backend selectors set to secret-key-shaped values: each is outside its
    // allowlist, so make_system_info reports "unknown" and the value vanishes.
    storage.metadata.backend = names[0] + "-" + marker;
    storage.cache.backend = names[1] + "-" + marker;
    storage.analytics.backend = names[2] + "-" + marker;
    storage.rate_limit.enabled = true;
    storage.rate_limit.backend = names[3] + "-" + marker;
    // Secret-bearing companion fields that must never be read by system.info.
    storage.metadata.sqlite_path = "/" + marker + "/path.db";
    storage.metadata.postgres_dsn = pg + "u:" + marker + at + "h/db";
    storage.cache.redis_address = rd + ":" + marker + at + "h";
    storage.rate_limit.redis_address = rd + ":" + marker + at + "h";

    ServerConfig server;
    server.tls.certificate_chain_file = "/tls/" + marker + "-cert.pem";
    server.tls.private_key_file = "/tls/" + marker + "-key.pem";
    server.tls.private_key_passphrase = marker + "-phrase";
    server.analytics_client_hash_salt = marker + "-salt";

    return make_system_info(storage, server);
}

// An ITableSource whose reads throw with a marker-bearing message, to prove the
// registry's exception isolation never copies what() into the error envelope.
class ThrowingTableSource final : public ITableSource
{
public:
    explicit ThrowingTableSource(std::string message)
        : m_message(std::move(message))
    {
    }

    const std::string& name() const override { return m_name; }
    bool writable() const override { return false; }
    const std::vector<TableColumn>& columns() const override
    {
        return m_columns;
    }
    Result<std::size_t> row_count() const override
    {
        throw std::runtime_error(m_message);
    }
    Result<TableRowsPage> rows(const TableQuery&) const override
    {
        throw std::runtime_error(m_message);
    }

private:
    std::string m_name = "links";  // an allowlisted name so add() accepts it
    std::string m_message;
    std::vector<TableColumn> m_columns;
};

}  // namespace

// Type: unit (system.info discloses only allowlisted names, T-53/C6).
// Scenario: a config whose backend fields are secret-key-shaped
//   (api_key/x_password/my_dsn/shared_secret) and whose DSN, sqlite path, redis
//   addresses, TLS files, passphrase and analytics salt all carry a marker is
//   serialized by system.info for a read-only User. The response contains none
//   of the secret-key-shaped names and no marker.
// Boundaries: the positive allowlist is the control - make_system_info
//   normalizes each backend string to "unknown" and copies only the named
//   non-secret fields, so the values never enter the serializer;
//   redactSecretValue is not reached (and would not match a `key`-shaped key).
// On failure first check: did make_system_info start echoing a raw backend
//   string or read a secret companion field, or did a new field open a channel?
BOOST_AUTO_TEST_CASE(system_info_emits_only_allowlisted_names)
{
    CommandRegistry registry;
    registry.add(make_system_info_descriptor(configWithSecrets()));

    const CommandResponse response =
        registry.execute(caller(ControlSet::User), request("system.info", "{}"));
    BOOST_REQUIRE(response.ok);

    std::vector<std::string> forbidden = secret_shaped_names();
    forbidden.push_back(fake_secret_marker());
    for (const std::string& value : forbidden) {
        BOOST_TEST(response.result_json.find(value) == std::string::npos,
                   "value leaked into system.info: " << value);
    }
}

// Type: unit (system.info has a closed key set, top level and shortener, C6).
// Scenario: a default snapshot serialized for an Admin yields a top-level key
//   set of exactly {version, backend, persistent, read_only, shortener}, and the
//   nested shortener object's key set equals exactly the nine documented
//   ShortenerSettings fields.
// Boundaries: the serializer emits a fixed catalog; there is no path by which a
//   config value becomes a new key, so adding a field anywhere is a deliberate
//   allowlist decision that must change this expectation.
// On failure first check: did serializeSystemInfoJson gain/drop a key, or did
//   ShortenerSettings change without updating this catalog?
BOOST_AUTO_TEST_CASE(system_info_has_no_extra_keys)
{
    CommandRegistry registry;
    registry.add(make_system_info_descriptor(
        make_system_info(StorageConfig {}, ServerConfig {})));

    const CommandResponse response = registry.execute(
        caller(ControlSet::Admin), request("system.info", "{}"));
    BOOST_REQUIRE(response.ok);

    const std::vector<std::string> expected_top = {
        "version", "backend", "persistent", "read_only", "shortener"};
    BOOST_TEST((top_level_keys(response.result_json) == expected_top));

    const std::string shortener =
        object_slice(response.result_json, "shortener");
    BOOST_REQUIRE(!shortener.empty());
    const std::vector<std::string> expected_shortener = {
        "base_domain",
        "generated_slug_length",
        "default_redirect_type",
        "default_expiry_seconds",
        "allow_private_targets",
        "analytics_enabled",
        "max_request_body_bytes",
        "max_request_target_length",
        "request_id_max_length"};
    BOOST_TEST((top_level_keys(shortener) == expected_shortener));
}

// Type: unit (exception text never reaches the error envelope, T-10/T-62).
// Scenario: db.tables runs over a catalog whose only source throws from
//   row_count() with a marker-bearing message. The response fails with the
//   generic internal error, and its message does not echo the marker.
// Boundaries: the registry's dispatch catch maps any escaping exception to
//   internal "internal error" with no what() copied out.
// On failure first check: did execute start surfacing e.what(), or did a new
//   path let the exception message into error.message?
BOOST_AUTO_TEST_CASE(error_messages_never_echo_secrets)
{
    TableCatalog catalog;
    BOOST_REQUIRE(catalog.add(std::make_shared<ThrowingTableSource>(
        fake_secret_marker() + "-in-what")));

    CommandRegistry registry;
    registry.add(make_db_tables_descriptor(catalog));

    const CommandResponse response =
        registry.execute(caller(ControlSet::User), request("db.tables", "{}"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::internal));
    BOOST_TEST(response.error.message.find(fake_secret_marker())
               == std::string::npos);
}
