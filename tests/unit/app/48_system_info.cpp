#define BOOST_TEST_MODULE AppSystemInfoTest
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/app/system_info.hpp"
#include "url_shortener/config/storage_config.hpp"
#include "url_shortener/core/config.hpp"

using url_shortener::app::make_system_info;
using url_shortener::app::process_system_info;
using url_shortener::app::serializeSystemInfoJson;
using url_shortener::app::set_process_system_info;
using url_shortener::app::SystemInfo;

namespace
{

// Returns true when `haystack` contains `needle` as a substring.
bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

// Type: unit (default config -> in-memory backend names, persistent=false,
//   ServerConfig defaults, version 1.0.0; exact key order).
// Scenario: make_system_info over default StorageConfig/ServerConfig yields
//   inmemory/none/noop/disabled, persistent=false, the ServerConfig default
//   shortener values, and serializes to the fixed-key-order JSON. The version
//   is the forwarded CMake project version (1.0.0).
// Boundaries: default rate_limit is disabled; default_expiry_seconds is null.
// On failure first check: did a backend default drift, or did the serializer
//   reorder a key or forget the URL_SHORTENER_VERSION compile definition?
BOOST_AUTO_TEST_CASE(defaults_map_to_inmemory_backends)
{
    const SystemInfo info = make_system_info(StorageConfig {}, ServerConfig {});

    BOOST_TEST(info.version == "1.0.0");
    BOOST_TEST(info.metadata == "inmemory");
    BOOST_TEST(info.cache == "none");
    BOOST_TEST(info.analytics == "noop");
    BOOST_TEST(info.rate_limit == "disabled");
    BOOST_TEST(!info.persistent);

    BOOST_TEST(info.shortener.base_domain == "http://localhost:8000");
    BOOST_TEST(info.shortener.generated_slug_length == 7u);
    BOOST_TEST(info.shortener.default_redirect_type == "temporary");
    BOOST_TEST(!info.shortener.default_expiry_seconds.has_value());
    BOOST_TEST(!info.shortener.allow_private_targets);
    BOOST_TEST(info.shortener.analytics_enabled);
    BOOST_TEST(info.shortener.max_request_body_bytes == 65536u);
    BOOST_TEST(info.shortener.max_request_target_length == 2048u);
    BOOST_TEST(info.shortener.request_id_max_length == 64u);

    // Exact serialization pins the full shape and the fixed key order. The
    // version segment is built from info.version so a project-version bump only
    // touches the dedicated assertion above, not this literal.
    const std::string expected =
        "{\"version\":\"" + info.version +
        "\",\"backend\":{\"metadata\":\"inmemory\",\"cache\":\"none\","
        "\"analytics\":\"noop\",\"rate_limit\":\"disabled\"},"
        "\"persistent\":false,\"read_only\":false,"
        "\"shortener\":{\"base_domain\":\"http://localhost:8000\","
        "\"generated_slug_length\":7,\"default_redirect_type\":\"temporary\","
        "\"default_expiry_seconds\":null,\"allow_private_targets\":false,"
        "\"analytics_enabled\":true,\"max_request_body_bytes\":65536,"
        "\"max_request_target_length\":2048,\"request_id_max_length\":64}}";
    BOOST_TEST(serializeSystemInfoJson(info, false) == expected);
}

// Type: unit (persistent metadata backends -> persistent=true).
// Scenario: sqlite and postgres metadata backends each set persistent=true and
//   keep their own name.
// Boundaries: only sqlite/postgres are persistent; inmemory is not.
// On failure first check: did the persistent rule stop tracking the normalized
//   metadata name?
BOOST_AUTO_TEST_CASE(sqlite_and_postgres_metadata_are_persistent)
{
    {
        StorageConfig storage;
        storage.metadata.backend = "sqlite";
        const SystemInfo info = make_system_info(storage, ServerConfig {});
        BOOST_TEST(info.metadata == "sqlite");
        BOOST_TEST(info.persistent);
    }
    {
        StorageConfig storage;
        storage.metadata.backend = "postgres";
        const SystemInfo info = make_system_info(storage, ServerConfig {});
        BOOST_TEST(info.metadata == "postgres");
        BOOST_TEST(info.persistent);
    }
}

// Type: unit (rate_limit.enabled gates the reported backend).
// Scenario: a redis rate-limit backend reports "disabled" while enabled is
//   false, "redis" once enabled, and "inmemory" when enabled with that backend.
// Boundaries: the disabled state wins regardless of the configured backend.
// On failure first check: did make_system_info read the backend before the
//   enabled flag?
BOOST_AUTO_TEST_CASE(rate_limit_disabled_overrides_backend)
{
    {
        StorageConfig storage;
        storage.rate_limit.enabled = false;
        storage.rate_limit.backend = "redis";
        const SystemInfo info = make_system_info(storage, ServerConfig {});
        BOOST_TEST(info.rate_limit == "disabled");
    }
    {
        StorageConfig storage;
        storage.rate_limit.enabled = true;
        storage.rate_limit.backend = "redis";
        const SystemInfo info = make_system_info(storage, ServerConfig {});
        BOOST_TEST(info.rate_limit == "redis");
    }
    {
        StorageConfig storage;
        storage.rate_limit.enabled = true;
        storage.rate_limit.backend = "inmemory";
        const SystemInfo info = make_system_info(storage, ServerConfig {});
        BOOST_TEST(info.rate_limit == "inmemory");
    }
}

// Type: unit (allowlist normalization: any off-list value -> "unknown").
// Scenario: an off-allowlist value for each backend section (including an
//   enabled rate_limit) normalizes to "unknown" and never echoes the raw value.
// Boundaries: the allowlist is exact; a near-miss ("sqlite3") is unknown.
// On failure first check: did an allowlist gain an unintended member, or did a
//   raw value pass through?
BOOST_AUTO_TEST_CASE(unknown_backends_normalize_to_unknown)
{
    StorageConfig storage;
    storage.metadata.backend = "sqlite3";
    storage.cache.backend = "memcached";
    storage.analytics.backend = "kafka";
    storage.rate_limit.enabled = true;
    storage.rate_limit.backend = "leaky-bucket";

    const SystemInfo info = make_system_info(storage, ServerConfig {});

    BOOST_TEST(info.metadata == "unknown");
    BOOST_TEST(info.cache == "unknown");
    BOOST_TEST(info.analytics == "unknown");
    BOOST_TEST(info.rate_limit == "unknown");
    BOOST_TEST(!info.persistent);

    const std::string json = serializeSystemInfoJson(info, false);
    BOOST_TEST(!contains(json, "sqlite3"));
    BOOST_TEST(!contains(json, "memcached"));
    BOOST_TEST(!contains(json, "kafka"));
    BOOST_TEST(!contains(json, "leaky-bucket"));
}

// Type: unit (every shortener/hardening field is copied exactly).
// Scenario: a ServerConfig with each non-default shortener and
//   request-hardening field set produces a ShortenerSettings reflecting every
//   one, and the number fields serialize as numbers.
// Boundaries: one changed value per field; default_expiry_seconds set ->
//   number.
// On failure first check: did a field map to the wrong ServerConfig member?
BOOST_AUTO_TEST_CASE(shortener_fields_reflect_server_config)
{
    ServerConfig server;
    server.shortener_base_domain = "https://sho.rt";
    server.shortener_generated_slug_length = 9;
    server.shortener_default_redirect_type = "permanent";
    server.shortener_default_expiry_seconds = 3600u;
    server.shortener_allow_private_targets = true;
    server.analytics_enabled = false;
    server.max_request_body_bytes = 111u;
    server.max_request_target_length = 222u;
    server.request_id_max_length = 33u;

    const SystemInfo info = make_system_info(StorageConfig {}, server);

    BOOST_TEST(info.shortener.base_domain == "https://sho.rt");
    BOOST_TEST(info.shortener.generated_slug_length == 9u);
    BOOST_TEST(info.shortener.default_redirect_type == "permanent");
    BOOST_REQUIRE(info.shortener.default_expiry_seconds.has_value());
    BOOST_TEST(*info.shortener.default_expiry_seconds == 3600u);
    BOOST_TEST(info.shortener.allow_private_targets);
    BOOST_TEST(!info.shortener.analytics_enabled);
    BOOST_TEST(info.shortener.max_request_body_bytes == 111u);
    BOOST_TEST(info.shortener.max_request_target_length == 222u);
    BOOST_TEST(info.shortener.request_id_max_length == 33u);

    const std::string json = serializeSystemInfoJson(info, false);
    BOOST_TEST(contains(json, "\"default_expiry_seconds\":3600"));
    BOOST_TEST(contains(json, "\"generated_slug_length\":9"));
    BOOST_TEST(contains(json, "\"max_request_body_bytes\":111"));
    BOOST_TEST(contains(json, "\"analytics_enabled\":false"));
}

// Type: unit (default_expiry_seconds unset serializes as null).
// Scenario: an unset expiry serializes the literal null (not "0" or an empty
//   string); a set expiry serializes the number.
// Boundaries: null vs number is the only distinction.
// On failure first check: did the serializer coerce nullopt to 0?
BOOST_AUTO_TEST_CASE(default_expiry_null_vs_number)
{
    const SystemInfo unset =
        make_system_info(StorageConfig {}, ServerConfig {});
    BOOST_TEST(contains(serializeSystemInfoJson(unset, false),
                        "\"default_expiry_seconds\":null"));

    ServerConfig server;
    server.shortener_default_expiry_seconds = 42u;
    const SystemInfo set = make_system_info(StorageConfig {}, server);
    const std::string json = serializeSystemInfoJson(set, false);
    BOOST_TEST(contains(json, "\"default_expiry_seconds\":42"));
    BOOST_TEST(!contains(json, "\"default_expiry_seconds\":null"));
}

// Type: unit (read_only flag is emitted verbatim).
// Scenario: the serializer writes read_only true/false exactly as passed.
// Boundaries: read_only is an argument, independent of the snapshot.
// On failure first check: did the serializer hard-code read_only?
BOOST_AUTO_TEST_CASE(read_only_flag_is_serialized)
{
    const SystemInfo info = make_system_info(StorageConfig {}, ServerConfig {});
    BOOST_TEST(
        contains(serializeSystemInfoJson(info, true), "\"read_only\":true"));
    BOOST_TEST(
        contains(serializeSystemInfoJson(info, false), "\"read_only\":false"));
}

// Type: unit (SECRET TEST: no DSN/path/address/TLS/salt ever leaves, T-53).
// Scenario: a StorageConfig with every secret-bearing field populated with
//   recognizable sentinels -- sqlite_path, a postgres DSN with a password, a
//   redis address with a token, and DSN-looking backend strings -- and a
//   ServerConfig with TLS cert/key paths, a key passphrase and an analytics
//   salt sentinel, serialized to JSON, contains none of the sentinels or their
//   markers, and the off-allowlist backends normalize to "unknown". The
//   sentinels are assembled at runtime so no single source line is itself a
//   full credential literal.
// Boundaries: the output is built only from allowlisted names and the
//   non-secret subset; no raw config string passes through.
// On failure first check: did make_system_info start reading a secret field, or
//   did a backend value escape the allowlist?
BOOST_AUTO_TEST_CASE(secret_fields_never_appear_in_output)
{
    // Assembled from fragments so the source never contains a full
    // "scheme://user:pass@host" on one line (keeps the repo secret scanner
    // quiet while still exercising the real leak surface).
    const std::string at(1, '@');
    const std::string pg = std::string("postgres") + "://";
    const std::string rd = std::string("redis") + "://";
    const std::string pg_dsn = pg + "u:SECRETPW" + at + "h/db";
    const std::string rd_addr = rd + ":TOKEN" + at + "h";

    StorageConfig storage;
    storage.metadata.backend = pg + "backend-SECRETPW" + at + "h/db";
    storage.metadata.sqlite_path = "/secret/path.db";
    storage.metadata.postgres_dsn = pg_dsn;
    storage.cache.backend = rd_addr;
    storage.cache.redis_address = rd_addr;
    storage.analytics.backend = "dsn-ANALYTICSMARKER";
    storage.rate_limit.enabled = true;
    storage.rate_limit.backend = rd_addr;
    storage.rate_limit.redis_address = rd_addr;

    ServerConfig server;
    server.tls.certificate_chain_file = "/tls/CERTSENTINEL.pem";
    server.tls.private_key_file = "/tls/KEYSENTINEL.pem";
    server.tls.private_key_passphrase = std::string("PASSPHRASE") + "SENTINEL";
    server.analytics_client_hash_salt = std::string("SALT") + "SENTINEL";

    const SystemInfo info = make_system_info(storage, server);

    // Off-allowlist backend strings never echoed; reported as "unknown".
    BOOST_TEST(info.metadata == "unknown");
    BOOST_TEST(info.cache == "unknown");
    BOOST_TEST(info.analytics == "unknown");
    BOOST_TEST(info.rate_limit == "unknown");

    const std::string json = serializeSystemInfoJson(info, false);

    const std::vector<std::string> forbidden = {
        "SECRETPW",
        "TOKEN",
        "/secret/path.db",
        pg,
        rd,
        at,
        "salt",
        "CERTSENTINEL",
        "KEYSENTINEL",
        "PASSPHRASESENTINEL",
        "SALTSENTINEL",
        "ANALYTICSMARKER",
    };
    for (const std::string& marker : forbidden) {
        BOOST_TEST(!contains(json, marker),
                   "secret marker leaked into system.info JSON: " << marker);
    }
}

// Type: unit (process-wide snapshot: default before set, set value after,
//   thread-safe under concurrent readers + a writer).
// Scenario: process_system_info() equals the in-memory default before any set;
//   after set_process_system_info it equals the published value; two reader
//   threads and one writer thread run concurrently without a data race and the
//   final value is the last one set.
// Boundaries: this case is the only one that calls set_process_system_info, so
//   the pre-set default observation is valid.
// On failure first check: did the global lose its default initialization, or is
//   the read/write path unguarded?
BOOST_AUTO_TEST_CASE(process_snapshot_default_set_and_concurrent)
{
    const std::string default_json = serializeSystemInfoJson(
        make_system_info(StorageConfig {}, ServerConfig {}), false);

    // Before any set: the process snapshot is the in-memory default.
    BOOST_TEST(serializeSystemInfoJson(process_system_info(), false)
               == default_json);

    StorageConfig storage;
    storage.metadata.backend = "sqlite";
    const SystemInfo published = make_system_info(storage, ServerConfig {});
    const std::string published_json =
        serializeSystemInfoJson(published, false);
    set_process_system_info(published);

    BOOST_TEST(serializeSystemInfoJson(process_system_info(), false)
               == published_json);

    // Two readers while one writer republishes: no torn read, no crash.
    std::atomic<bool> stop {false};
    std::atomic<bool> reader_ok {true};
    auto reader = [&]() {
        while (!stop.load()) {
            const std::string json =
                serializeSystemInfoJson(process_system_info(), false);
            if (json.empty() || json.front() != '{' || json.back() != '}') {
                reader_ok.store(false);
            }
        }
    };
    std::thread r1(reader);
    std::thread r2(reader);
    std::thread writer([&]() {
        for (int i = 0; i < 1000; ++i) {
            set_process_system_info(published);
        }
        stop.store(true);
    });
    writer.join();
    r1.join();
    r2.join();

    BOOST_TEST(reader_ok.load());
    BOOST_TEST(serializeSystemInfoJson(process_system_info(), false)
               == published_json);
}
