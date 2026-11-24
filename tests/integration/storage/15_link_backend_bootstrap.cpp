/**
 * @file 15_link_backend_bootstrap.cpp
 * @brief Integration tests for `app::InitLinkBackend` / `LinkBackendHandle`
 * (subtask 02.0/11, backend bootstrap).
 *
 * Exercises the one function that turns `ServerConfig::storage_config_path`
 * into a live, installed `RepositoryLinkStore`: the inactive default path,
 * the in-memory and SQLite active paths (including persistence across a
 * simulated process restart), the fail-closed startup errors with their
 * fixed secret-free messages, the redacted summary, the destructor's final
 * write-behind flush, and the carried security requirements (T-30 file mode,
 * T-31 parse-error redaction, duplicate-slug mapping, serving latch /
 * `LinkBackend::shutdown`).
 *
 * File isolation: every SQLite file and YAML path carries the process id and
 * a per-call counter so overlapping `ctest` runs never collide; each case
 * resets `LinkBackend` and removes its temp files.
 */
#define BOOST_TEST_MODULE LinkBackendBootstrap
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/app/system_info.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/core/types.hpp>

#ifdef _WIN32
#    include <process.h>
#else
#    include <sys/stat.h>
#    include <unistd.h>
#endif

namespace
{

using url_shortener::app::InitLinkBackend;
using url_shortener::app::LinkBackend;
using url_shortener::app::LinkBackendHandle;
using url_shortener::app::make_system_info;
using url_shortener::app::process_system_info;
using url_shortener::app::set_process_system_info;
using url_shortener::app::summarize_storage_config;

/// Monotonic counter making each temp path within one process unique.
std::atomic<long> g_unique {0};

/**
 * @brief Returns this process's id for unique temp-path construction.
 * @return The current process id.
 */
long current_pid()
{
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

/**
 * @brief Builds a unique temp path with the given suffix.
 * @param suffix Trailing filename component (e.g. `".db"`, `".yaml"`).
 * @return A collision-free absolute path in the temp directory.
 */
std::filesystem::path temp_path(const std::string& suffix)
{
    return std::filesystem::temp_directory_path()
        / ("15_lbb_" + std::to_string(current_pid()) + "_"
           + std::to_string(g_unique.fetch_add(1)) + suffix);
}

/**
 * @brief Writes @p content to @p path, overwriting any existing file.
 * @param path Destination file path.
 * @param content Bytes to write.
 */
void write_file(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

/**
 * @brief Builds a minimal valid, already-validated link for persistence.
 * @param slug Slug / id seed.
 * @return A link with the fields the codec requires.
 */
url_shortener::Link make_link(const std::string& slug)
{
    url_shortener::Link link;
    link.id = "id-" + slug;
    link.slug = slug;
    link.target_url = "https://example.com/" + slug;
    link.created_at = "2026-01-01T00:00:00Z";
    link.updated_at = link.created_at;
    link.enabled = true;
    return link;
}

/**
 * @brief RAII guard that resets `LinkBackend` on scope exit so one case's
 * installation never leaks into the next.
 */
struct BackendResetGuard
{
    ~BackendResetGuard() { LinkBackend::reset(); }
};

#ifndef _WIN32
/**
 * @brief Asserts @p path is mode 0600: owner read+write and no group or
 * other permission bits.
 * @param path File to check.
 */
void check_owner_only(const std::filesystem::path& path)
{
    const auto perms = std::filesystem::status(path).permissions();
    BOOST_CHECK((perms & std::filesystem::perms::owner_read)
                != std::filesystem::perms::none);
    BOOST_CHECK((perms & std::filesystem::perms::owner_write)
                != std::filesystem::perms::none);
    BOOST_CHECK((perms & std::filesystem::perms::group_all)
                == std::filesystem::perms::none);
    BOOST_CHECK((perms & std::filesystem::perms::others_all)
                == std::filesystem::perms::none);
}

/**
 * @brief Reads the current process umask without permanently changing it.
 * @return The umask value in effect on entry.
 */
mode_t read_umask()
{
    const mode_t current = ::umask(0);
    ::umask(current);
    return current;
}
#endif

}  // namespace

/**
 * [Integration][App] No configured path leaves the holder untouched.
 *
 * Scenario: InitLinkBackend with an unset storage_config_path.
 * Boundaries: the inactive / default (legacy) path (C3/C4).
 * On failure first check: InitLinkBackend's early return and that nothing
 * is installed.
 */
BOOST_AUTO_TEST_CASE(no_path_is_inactive)
{
    BackendResetGuard guard;
    ServerConfig config;  // storage_config_path unset
    LinkBackendHandle handle = InitLinkBackend(config);
    BOOST_CHECK(!handle.active());
    BOOST_CHECK(!LinkBackend::installed());
    BOOST_CHECK(handle.summary().empty());
}

/**
 * [Integration][App] In-memory metadata installs a write-through store.
 *
 * Scenario: InitLinkBackend with `metadata: {backend: inmemory}`.
 * Boundaries: settings ttl/flush are 0 (write-through, in-memory only);
 * create + find flow through the installed store.
 * On failure first check: settings resolution for the non-persistent path
 * and LinkBackend::store() returning the installed store.
 */
BOOST_AUTO_TEST_CASE(inmemory_active_and_write_through)
{
    BackendResetGuard guard;
    const auto yaml = temp_path(".yaml");
    write_file(yaml, "metadata:\n  backend: inmemory\n");

    ServerConfig config;
    config.storage_config_path = yaml.string();
    LinkBackendHandle handle = InitLinkBackend(config);

    BOOST_CHECK(handle.active());
    BOOST_CHECK(LinkBackend::installed());
    BOOST_CHECK(handle.settings().l1_ttl == std::chrono::seconds(0));
    BOOST_CHECK(handle.settings().stats_flush_interval
                == std::chrono::seconds(0));

    auto& store = LinkBackend::store();
    BOOST_REQUIRE(store.create(make_link("mem1")));
    const auto found = store.findBySlug("mem1");
    BOOST_REQUIRE(found.has_value());
    BOOST_CHECK_EQUAL(found->target_url, "https://example.com/mem1");

    // Write-through: recordRedirect persists with no background thread.
    BOOST_CHECK(store.recordRedirect(*found));
    const auto after = store.findBySlug("mem1");
    BOOST_REQUIRE(after.has_value());
    BOOST_CHECK_GE(after->stats.total_redirects, 1u);

    std::filesystem::remove(yaml);
}

/**
 * [Integration][App] SQLite backend persists across a simulated restart.
 *
 * Scenario: create through the installed store over a SQLite file, destroy
 * the handle, reset the holder, re-run InitLinkBackend with the same YAML.
 * Boundaries: persistence across "processes"; persistent settings 5 s / 30 s.
 * On failure first check: the probe's lazy schema bootstrap and that a fresh
 * store re-opens the same file.
 */
BOOST_AUTO_TEST_CASE(sqlite_persists_across_restart)
{
    BackendResetGuard guard;
    const auto db = temp_path(".db");
    const auto yaml = temp_path(".yaml");
    std::filesystem::remove(db);
    write_file(
        yaml,
        "metadata:\n  backend: sqlite\n  sqlite_path: " + db.string() + "\n");

    ServerConfig config;
    config.storage_config_path = yaml.string();

    {
        LinkBackendHandle handle = InitLinkBackend(config);
        BOOST_CHECK(handle.active());
        BOOST_CHECK(handle.settings().l1_ttl == std::chrono::seconds(5));
        BOOST_CHECK(handle.settings().stats_flush_interval
                    == std::chrono::seconds(30));
        BOOST_REQUIRE(LinkBackend::store().create(make_link("disk1")));
    }
    LinkBackend::reset();

    {
        LinkBackendHandle handle = InitLinkBackend(config);
        const auto found = LinkBackend::store().findBySlug("disk1");
        BOOST_REQUIRE(found.has_value());
        BOOST_CHECK_EQUAL(found->target_url, "https://example.com/disk1");
    }
    LinkBackend::reset();

    std::filesystem::remove(db);
    std::filesystem::remove(yaml);
}

/**
 * [Integration][App] A missing YAML file fails closed with a fixed message.
 *
 * Scenario: InitLinkBackend pointed at a path that does not exist.
 * Boundaries: fixed `storage config:` prefix; no file contents in the
 * message; nothing installed.
 * On failure first check: the YAML::BadFile branch of parse_config_or_throw.
 */
BOOST_AUTO_TEST_CASE(missing_file_throws_fixed_message)
{
    BackendResetGuard guard;
    ServerConfig config;
    config.storage_config_path = temp_path(".yaml").string();  // never created

    try {
        InitLinkBackend(config);
        BOOST_FAIL("expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        BOOST_CHECK(msg.rfind("storage config:", 0) == 0);
        BOOST_CHECK_EQUAL(msg, "storage config: cannot read file");
    }
    BOOST_CHECK(!LinkBackend::installed());
}

/**
 * [Integration][App] An invalid backend name names the backend, not the file.
 *
 * Scenario: YAML with `metadata.backend: foo`.
 * Boundaries: the validation (Require) branch; the message echoes the backend
 * token only.
 * On failure first check: the std::runtime_error branch of
 * parse_config_or_throw forwarding the field-only Require message.
 */
BOOST_AUTO_TEST_CASE(invalid_backend_names_backend)
{
    BackendResetGuard guard;
    const auto yaml = temp_path(".yaml");
    write_file(yaml, "metadata:\n  backend: foo\n");

    ServerConfig config;
    config.storage_config_path = yaml.string();
    try {
        InitLinkBackend(config);
        BOOST_FAIL("expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        BOOST_CHECK(msg.rfind("storage config:", 0) == 0);
        BOOST_CHECK(msg.find("foo") != std::string::npos);
    }
    BOOST_CHECK(!LinkBackend::installed());
    std::filesystem::remove(yaml);
}

/**
 * [Integration][App] A SQLite path in a nonexistent directory fails the probe.
 *
 * Scenario: sqlite_path under a directory that does not exist.
 * Boundaries: the probe maps the open failure to the fixed
 * `storage backend unavailable`; nothing is installed after the throw.
 * On failure first check: the probe's error handling in InitLinkBackend.
 */
BOOST_AUTO_TEST_CASE(sqlite_bad_dir_fails_probe)
{
    BackendResetGuard guard;
    const auto bad = temp_path("_nodir") / "inner" / "x.db";
    const auto yaml = temp_path(".yaml");
    write_file(
        yaml,
        "metadata:\n  backend: sqlite\n  sqlite_path: " + bad.string() + "\n");

    ServerConfig config;
    config.storage_config_path = yaml.string();
    try {
        InitLinkBackend(config);
        BOOST_FAIL("expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        BOOST_CHECK_EQUAL(std::string(e.what()), "storage backend unavailable");
    }
    BOOST_CHECK(!LinkBackend::installed());
    std::filesystem::remove(yaml);
}

/**
 * [Integration][App] summarize_storage_config / summary() leak no secrets.
 *
 * Scenario: a StorageConfig built in memory whose postgres_dsn carries a
 * recognizable password token, plus a sqlite_path and a redis_address.
 * Boundaries: the redacted summary contains backend names only — never the
 * DSN, host, token, path, or address (CLAUDE.md, T-31 sibling).
 * On failure first check: summarize_storage_config's field selection.
 */
BOOST_AUTO_TEST_CASE(summary_redacts_secrets)
{
    StorageConfig cfg;
    cfg.metadata.backend = "postgres";
    cfg.metadata.postgres_dsn =
        "postgresql://admin:s3cr3t-token@db.internal.example:5432/links";
    cfg.cache.backend = "redis";
    cfg.cache.redis_address = "cache.internal.example:6379";
    cfg.analytics.backend = "inmemory";
    cfg.rate_limit.enabled = false;

    const std::string summary = summarize_storage_config(cfg);
    BOOST_CHECK_EQUAL(
        summary,
        "metadata=postgres cache=redis analytics=inmemory rate_limit=off");
    BOOST_CHECK(summary.find("s3cr3t-token") == std::string::npos);
    BOOST_CHECK(summary.find("db.internal.example") == std::string::npos);
    BOOST_CHECK(summary.find("cache.internal.example") == std::string::npos);
    BOOST_CHECK(summary.find("6379") == std::string::npos);

    // sqlite_path must not leak either.
    StorageConfig sqlite_cfg;
    sqlite_cfg.metadata.backend = "sqlite";
    sqlite_cfg.metadata.sqlite_path = "/var/secret/links-private.db";
    const std::string sqlite_summary = summarize_storage_config(sqlite_cfg);
    BOOST_CHECK(sqlite_summary.find("links-private") == std::string::npos);
    BOOST_CHECK(sqlite_summary.find("/var/secret") == std::string::npos);

    // An active handle's summary() equals the pure function on its config.
    BackendResetGuard guard;
    const auto db = temp_path(".db");
    const auto yaml = temp_path(".yaml");
    std::filesystem::remove(db);
    write_file(
        yaml,
        "metadata:\n  backend: sqlite\n  sqlite_path: " + db.string() + "\n");
    ServerConfig server_config;
    server_config.storage_config_path = yaml.string();
    LinkBackendHandle handle = InitLinkBackend(server_config);
    StorageConfig active_cfg;
    active_cfg.metadata.backend = "sqlite";
    active_cfg.metadata.sqlite_path = db.string();
    BOOST_CHECK_EQUAL(handle.summary(), summarize_storage_config(active_cfg));
    BOOST_CHECK(handle.summary().find(db.string()) == std::string::npos);

    LinkBackend::reset();
    std::filesystem::remove(db);
    std::filesystem::remove(yaml);
}

/**
 * [Integration][App] Non-secret YAML parse failure is mapped to a fixed,
 * value-free message (T-31).
 *
 * Scenario: YAML malformed next to a postgres_dsn carrying a password token,
 * forcing a yaml-cpp parse/conversion error rather than a Require failure.
 * Boundaries: the thrown message is the fixed `storage config: invalid
 * configuration` and contains neither the token nor the host.
 * On failure first check: the YAML::Exception branch of parse_config_or_throw
 * (raw e.what() must never be surfaced).
 */
BOOST_AUTO_TEST_CASE(parse_error_does_not_leak_dsn)
{
    BackendResetGuard guard;
    const auto yaml = temp_path(".yaml");
    // `rate_limit.enabled` expects a bool; a scalar that cannot convert
    // raises a yaml-cpp BadConversion whose what() can echo nearby scalars.
    write_file(yaml,
               "metadata:\n"
               "  backend: postgres\n"
               "  postgres_dsn: "
               "postgresql://admin:s3cr3t-token@db.internal.example/links\n"
               "rate_limit:\n"
               "  enabled: not-a-bool-s3cr3t-token\n");

    ServerConfig config;
    config.storage_config_path = yaml.string();
    try {
        InitLinkBackend(config);
        BOOST_FAIL("expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        BOOST_CHECK_EQUAL(msg, "storage config: invalid configuration");
        BOOST_CHECK(msg.find("s3cr3t-token") == std::string::npos);
        BOOST_CHECK(msg.find("db.internal.example") == std::string::npos);
    }
    BOOST_CHECK(!LinkBackend::installed());
    std::filesystem::remove(yaml);
}

/**
 * [Integration][App] The destructor's final flush persists pending counters.
 *
 * Scenario: over SQLite, create + recordRedirect, then destroy the handle
 * (stop_stats_flusher joins the thread and runs one final flush); reopen and
 * read the persisted counter.
 * Boundaries: write-behind persistence on teardown; the flusher thread is
 * joined (no thread left running after the handle is gone).
 * On failure first check: LinkBackendHandle::~ calling stop_stats_flusher and
 * the final flush writing the stats attribute.
 */
BOOST_AUTO_TEST_CASE(destructor_flushes_counter)
{
    BackendResetGuard guard;
    const auto db = temp_path(".db");
    const auto yaml = temp_path(".yaml");
    std::filesystem::remove(db);
    write_file(
        yaml,
        "metadata:\n  backend: sqlite\n  sqlite_path: " + db.string() + "\n");

    ServerConfig config;
    config.storage_config_path = yaml.string();
    {
        LinkBackendHandle handle = InitLinkBackend(config);
        auto& store = LinkBackend::store();
        BOOST_REQUIRE(store.create(make_link("hit1")));
        const auto link = store.findBySlug("hit1");
        BOOST_REQUIRE(link.has_value());
        BOOST_CHECK(store.recordRedirect(*link));
        // handle destroyed here -> final flush persists the delta.
    }
    LinkBackend::reset();

    {
        LinkBackendHandle handle = InitLinkBackend(config);
        const auto reread = LinkBackend::store().findBySlug("hit1");
        BOOST_REQUIRE(reread.has_value());
        BOOST_CHECK_EQUAL(reread->stats.total_redirects, 1u);
    }
    LinkBackend::reset();

    std::filesystem::remove(db);
    std::filesystem::remove(yaml);
}

/**
 * [Integration][App] Moving an active handle keeps exactly one final flush.
 *
 * Scenario: create + recordRedirect, move the handle into a second, let the
 * second destruct; the moved-from source must not flush or re-stop.
 * Boundaries: move semantics; a single persisted count after a move.
 * On failure first check: the move constructor nulling the source store.
 */
BOOST_AUTO_TEST_CASE(move_keeps_single_flush)
{
    BackendResetGuard guard;
    const auto db = temp_path(".db");
    const auto yaml = temp_path(".yaml");
    std::filesystem::remove(db);
    write_file(
        yaml,
        "metadata:\n  backend: sqlite\n  sqlite_path: " + db.string() + "\n");

    ServerConfig config;
    config.storage_config_path = yaml.string();
    {
        LinkBackendHandle first = InitLinkBackend(config);
        BOOST_REQUIRE(LinkBackend::store().create(make_link("mv1")));
        const auto link = LinkBackend::store().findBySlug("mv1");
        BOOST_REQUIRE(link.has_value());
        BOOST_CHECK(LinkBackend::store().recordRedirect(*link));

        LinkBackendHandle second = std::move(first);
        BOOST_CHECK(!first.active());
        BOOST_CHECK(second.active());
        // second destructs here (one flush); first destructs as inactive.
    }
    LinkBackend::reset();

    {
        LinkBackendHandle handle = InitLinkBackend(config);
        const auto reread = LinkBackend::store().findBySlug("mv1");
        BOOST_REQUIRE(reread.has_value());
        BOOST_CHECK_EQUAL(reread->stats.total_redirects, 1u);
    }
    LinkBackend::reset();

    std::filesystem::remove(db);
    std::filesystem::remove(yaml);
}

/**
 * [Integration][App] A duplicate slug maps to slug_conflict, not
 * storage_failure (carried requirement 6).
 *
 * Scenario: create the same slug twice over SQLite.
 * Boundaries: a DB unique-constraint violation maps through the error mapper
 * (already_exists) and the store (slug_conflict / 409), never storage_failure
 * (500).
 * On failure first check: SqliteErrorMapper's UNIQUE case and
 * RepositoryLinkStore::create's RepoError translation.
 */
BOOST_AUTO_TEST_CASE(duplicate_slug_is_conflict)
{
    BackendResetGuard guard;
    const auto db = temp_path(".db");
    const auto yaml = temp_path(".yaml");
    std::filesystem::remove(db);
    write_file(
        yaml,
        "metadata:\n  backend: sqlite\n  sqlite_path: " + db.string() + "\n");

    ServerConfig config;
    config.storage_config_path = yaml.string();
    LinkBackendHandle handle = InitLinkBackend(config);
    auto& store = LinkBackend::store();
    BOOST_REQUIRE(store.create(make_link("dup1")));

    url_shortener::app::AppError error;
    BOOST_CHECK(!store.create(make_link("dup1"), &error));
    BOOST_CHECK(error.code == url_shortener::app::AppErrorCode::slug_conflict);

    LinkBackend::reset();
    std::filesystem::remove(db);
    std::filesystem::remove(yaml);
}

#ifndef _WIN32
/**
 * [Integration][App] A freshly created SQLite file is mode 0600; an existing
 * file's permissions are never widened (carried requirement 1, T-30).
 *
 * Scenario: bootstrap creating a new DB, then bootstrap over a pre-existing
 * group-readable file.
 * Boundaries: new file -> owner rw only; pre-existing file -> unchanged.
 * On failure first check: restrict_new_sqlite_file's pre_existed gate.
 */
BOOST_AUTO_TEST_CASE(new_sqlite_file_is_0600)
{
    BackendResetGuard guard;
    const auto db = temp_path(".db");
    const auto yaml = temp_path(".yaml");
    std::filesystem::remove(db);
    write_file(
        yaml,
        "metadata:\n  backend: sqlite\n  sqlite_path: " + db.string() + "\n");

    ServerConfig config;
    config.storage_config_path = yaml.string();
    {
        LinkBackendHandle handle = InitLinkBackend(config);
    }
    LinkBackend::reset();

    const auto perms = std::filesystem::status(db).permissions();
    BOOST_CHECK((perms & std::filesystem::perms::owner_read)
                != std::filesystem::perms::none);
    BOOST_CHECK((perms & std::filesystem::perms::owner_write)
                != std::filesystem::perms::none);
    BOOST_CHECK((perms & std::filesystem::perms::group_all)
                == std::filesystem::perms::none);
    BOOST_CHECK((perms & std::filesystem::perms::others_all)
                == std::filesystem::perms::none);

    std::filesystem::remove(db);

    // Pre-existing group-readable file: perms must not be widened/narrowed.
    const auto db2 = temp_path(".db");
    write_file(db2, "");  // empty file == empty SQLite DB
    std::filesystem::permissions(db2,
                                 std::filesystem::perms::owner_read
                                     | std::filesystem::perms::owner_write
                                     | std::filesystem::perms::group_read,
                                 std::filesystem::perm_options::replace);
    write_file(
        yaml,
        "metadata:\n  backend: sqlite\n  sqlite_path: " + db2.string() + "\n");
    config.storage_config_path = yaml.string();
    {
        LinkBackendHandle handle = InitLinkBackend(config);
    }
    LinkBackend::reset();
    const auto perms2 = std::filesystem::status(db2).permissions();
    BOOST_CHECK((perms2 & std::filesystem::perms::group_read)
                != std::filesystem::perms::none);

    std::filesystem::remove(db2);
    std::filesystem::remove(yaml);
}

/**
 * [Integration][App] The new DB and any journal companion are owner-only,
 * and the process umask is restored after InitLinkBackend returns (T-30).
 *
 * Scenario: with a known permissive process umask (0022), bootstrap a fresh
 * SQLite backend and perform one write (forcing schema + a rollback
 * journal), then inspect the main file and any `-journal`/`-wal`/`-shm`
 * companion still on disk.
 * Boundaries: the umask guard makes every file SQLite creates on first open
 * born 0600 (not just the chmod-ed main file), and the guard restores the
 * prior umask on return; companions are short-lived, so present ones are
 * checked and absent ones skipped.
 * On failure first check: the ScopedRestrictiveUmask around the probe in
 * InitLinkBackend and that it restores the previous umask.
 */
BOOST_AUTO_TEST_CASE(new_sqlite_file_companions_owner_only_umask_restored)
{
    BackendResetGuard guard;
    const mode_t prior = ::umask(022);  // deterministic permissive umask
    const auto db = temp_path(".db");
    const auto yaml = temp_path(".yaml");
    std::filesystem::remove(db);
    write_file(
        yaml,
        "metadata:\n  backend: sqlite\n  sqlite_path: " + db.string() + "\n");

    ServerConfig config;
    config.storage_config_path = yaml.string();
    static const char* const k_companions[] = {"-journal", "-wal", "-shm"};
    {
        LinkBackendHandle handle = InitLinkBackend(config);
        // Process umask restored to the pre-call value once the guard
        // around the probe has gone out of scope.
        BOOST_CHECK_EQUAL(read_umask(), static_cast<mode_t>(022));

        // A write forces the schema + a rollback journal to be created.
        auto& store = LinkBackend::store();
        BOOST_REQUIRE(store.create(make_link("perm1")));

        check_owner_only(db);
        for (const char* suffix : k_companions) {
            const std::filesystem::path companion = db.string() + suffix;
            std::error_code ec;
            if (std::filesystem::exists(companion, ec) && !ec) {
                check_owner_only(companion);
            }
        }
    }
    LinkBackend::reset();

    std::filesystem::remove(db);
    for (const char* suffix : k_companions) {
        std::error_code ec;
        std::filesystem::remove(db.string() + suffix, ec);
    }
    std::filesystem::remove(yaml);
    ::umask(prior);
}

/**
 * [Integration][App] The process umask is restored even when the probe
 * fails and InitLinkBackend throws (T-30, RAII guard on the unwind path).
 *
 * Scenario: with a known umask (0022), point a SQLite backend at a path
 * under a nonexistent directory so the probe fails closed.
 * Boundaries: InitLinkBackend throws, yet the ScopedRestrictiveUmask's
 * destructor restores the prior umask during stack unwinding.
 * On failure first check: ScopedRestrictiveUmask restoring the umask on
 * exception, not only on normal return.
 */
BOOST_AUTO_TEST_CASE(umask_restored_after_probe_failure)
{
    BackendResetGuard guard;
    const mode_t prior = ::umask(022);
    const auto bad = temp_path("_nodir") / "inner" / "x.db";
    const auto yaml = temp_path(".yaml");
    write_file(
        yaml,
        "metadata:\n  backend: sqlite\n  sqlite_path: " + bad.string() + "\n");

    ServerConfig config;
    config.storage_config_path = yaml.string();
    BOOST_CHECK_THROW(InitLinkBackend(config), std::runtime_error);
    BOOST_CHECK_EQUAL(read_umask(), static_cast<mode_t>(022));
    BOOST_CHECK(!LinkBackend::installed());

    std::filesystem::remove(yaml);
    ::umask(prior);
}
#endif

/**
 * [Integration][App] A throwing flusher start leaves nothing installed
 * (fail-closed rollback, LOW follow-up fix 2).
 *
 * Scenario: inject a throwing post-install start step via the bootstrap
 * test hook, then run InitLinkBackend over an in-memory backend.
 * Boundaries: install() has published the store when the start step throws;
 * the catch must run LinkBackend::shutdown() so nothing stays installed.
 * On failure first check: the try/catch around install + flusher start in
 * InitLinkBackend calling LinkBackend::shutdown() before rethrowing.
 */
BOOST_AUTO_TEST_CASE(flusher_start_failure_rolls_back_install)
{
    BackendResetGuard guard;
    const auto yaml = temp_path(".yaml");
    write_file(yaml, "metadata:\n  backend: inmemory\n");

    ServerConfig config;
    config.storage_config_path = yaml.string();

    // Clear the hook no matter how the case exits so it never leaks.
    struct HookReset
    {
        ~HookReset()
        {
            url_shortener::app::detail::set_start_flusher_hook_for_testing(
                nullptr);
        }
    } hook_reset;
    url_shortener::app::detail::set_start_flusher_hook_for_testing(
        [] { throw std::runtime_error("injected flusher start failure"); });

    BOOST_CHECK_THROW(InitLinkBackend(config), std::runtime_error);
    BOOST_CHECK(!LinkBackend::installed());

    std::filesystem::remove(yaml);
}

/**
 * [Unit][App] The serving latch and LinkBackend::shutdown stop and release the
 * installed store (carried requirement 5).
 *
 * Scenario: install via bootstrap, mark_serving, then shutdown.
 * Boundaries: serving() tracks the latch; shutdown() clears it, stops the
 * flusher, and uninstalls so the store is released on the main thread.
 * On failure first check: LinkBackend::shutdown clearing s_serving before the
 * inline reset and dropping s_owner.
 */
BOOST_AUTO_TEST_CASE(serving_latch_and_shutdown)
{
    BackendResetGuard guard;
    const auto db = temp_path(".db");
    const auto yaml = temp_path(".yaml");
    std::filesystem::remove(db);
    write_file(
        yaml,
        "metadata:\n  backend: sqlite\n  sqlite_path: " + db.string() + "\n");

    ServerConfig config;
    config.storage_config_path = yaml.string();
    LinkBackendHandle handle = InitLinkBackend(config);
    BOOST_CHECK(LinkBackend::installed());
    BOOST_CHECK(!LinkBackend::serving());

    LinkBackend::mark_serving();
    BOOST_CHECK(LinkBackend::serving());

    LinkBackend::shutdown();
    BOOST_CHECK(!LinkBackend::serving());
    BOOST_CHECK(!LinkBackend::installed());

    // The handle still owns the (now stopped) store; its destructor's
    // stop_stats_flusher is idempotent and safe after shutdown.
    std::filesystem::remove(db);
    std::filesystem::remove(yaml);
}

/**
 * [Integration][App] InitLinkBackend publishes the process-wide system.info
 * snapshot from the resolved config (subtask 03.0/18 follow-up).
 *
 * Scenario: bootstrap over a SQLite config and read process_system_info();
 * then bootstrap with no config and read it again.
 * Boundaries: the SQLite path yields metadata "sqlite" / persistent true; the
 * no-config path yields metadata "inmemory" / persistent false from the default
 * StorageConfig, so system.info no longer always reports the in-memory default.
 * On failure first check: the set_process_system_info call in both branches of
 * InitLinkBackend.
 */
BOOST_AUTO_TEST_CASE(bootstrap_publishes_system_info_snapshot)
{
    // Restore the process-wide snapshot to the default on scope exit so later
    // tests that rely on the untouched default are not affected.
    struct SnapshotReset
    {
        ~SnapshotReset()
        {
            set_process_system_info(
                make_system_info(StorageConfig {}, ServerConfig {}));
        }
    } snapshot_reset;

    BackendResetGuard guard;
    const auto db = temp_path(".db");
    const auto yaml = temp_path(".yaml");
    std::filesystem::remove(db);
    write_file(
        yaml,
        "metadata:\n  backend: sqlite\n  sqlite_path: " + db.string() + "\n");

    ServerConfig config;
    config.storage_config_path = yaml.string();
    {
        LinkBackendHandle handle = InitLinkBackend(config);
        const auto info = process_system_info();
        BOOST_CHECK_EQUAL(info.metadata, "sqlite");
        BOOST_CHECK(info.persistent);
    }
    LinkBackend::reset();

    // No configured path -> the default StorageConfig snapshot (inmemory).
    ServerConfig default_config;  // storage_config_path unset
    LinkBackendHandle inactive = InitLinkBackend(default_config);
    const auto default_info = process_system_info();
    BOOST_CHECK_EQUAL(default_info.metadata, "inmemory");
    BOOST_CHECK(!default_info.persistent);

    std::filesystem::remove(db);
    std::filesystem::remove(yaml);
}
