/**
 * @file 70_db_table_allowlist_test.cpp
 * @brief Gap cases for the `db.*` table allowlist (milestone contract C6, threat
 * model T-51): `db.rows` resolves `table` only through the positive allowlist
 * (`app::TableCatalog::find`, `db_commands.cpp` `handle_db_rows`), so a hostile,
 * case-shifted, injection-shaped, path-shaped or empty name all collapse to one
 * identical `not_found` and never a `storage_failure`, and an unknown name is
 * indistinguishable from a real-but-forbidden one (no existence oracle).
 *
 * These are the two cases not already proven by `commands__27_db_commands` and
 * `commands__30_browse_security_regression`: an extended hostile-name list and
 * the no-oracle byte-equality of two refusals. The registry is the real one the
 * process serves, built by `composition::build_command_runtime` over the
 * in-memory backend, so the allowlist under test is the production allowlist
 * (`links`, `click_events`, `jobs`), not a test-local catalog.
 */
#define BOOST_TEST_MODULE CommandsDbTableAllowlistTest
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/app/link_backend.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/composition/command_runtime.hpp"
#include "url_shortener/composition/link_backend_bootstrap.hpp"
#include "url_shortener/core/config.hpp"

using url_shortener::app::LinkBackend;
using url_shortener::app::LinkBackendHandle;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::serialize_response;
using url_shortener::composition::build_command_runtime;
using url_shortener::composition::CommandRuntime;

namespace
{

// A read-only User caller: db.* requires only Permission::read, so a refusal
// here is the allowlist speaking, never an authorization denial.
CallerContext user_caller()
{
    CallerContext caller;
    caller.principal = "test-user";
    caller.transport = "test";
    caller.control_set = ControlSet::User;
    return caller;
}

// A db.rows request with a fixed id, so two refusals differ only by table name
// (the no-oracle comparison depends on the id being identical).
CommandRequest db_rows(const std::string& table_json_value)
{
    CommandRequest req;
    req.id = "client-1";
    req.command = "db.rows";
    req.args_json = R"({"table":")" + table_json_value + R"("})";
    return req;
}

}  // namespace

// Type: unit (every non-allowlisted name -> one identical not_found, T-51).
// Scenario: auth tables, generic/schema catalog tables, a cross-schema
//   qualified name, an injection-shaped name, a case variant and a trailing-
//   space variant of a real table, a path-shaped name, and the empty string are
//   each resolved through db.rows against the production allowlist (links,
//   click_events, jobs). Every one fails with not_found and the fixed message
//   "table not found", never storage_failure, and never echoes the requested
//   name.
// Boundaries: the catalog find() matches byte-for-byte, so "LINKS", "links "
//   and "../links" are all misses; the failure code is a single value across
//   the whole hostile set.
// On failure first check: did find() start matching loosely/trimming, did a
//   name reach the backend (storage_failure), or did the message start quoting
//   the argument?
BOOST_AUTO_TEST_CASE(rows_rejects_every_non_allowlisted_name)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = build_command_runtime(server, backend);

    const std::vector<std::string> names = {
        "users",
        "sessions",
        "auth_audit_log",
        "schema_migrations",
        "sqlite_master",
        "pg_catalog.pg_user",
        "links;DROP",
        "LINKS",
        "links ",  // trailing space
        "../links",
    };

    for (const std::string& name : names) {
        BOOST_TEST_CONTEXT("table=" << name)
        {
            const CommandResponse response =
                runtime.registry->execute(user_caller(), db_rows(name));

            BOOST_TEST(!response.ok);
            BOOST_TEST((response.error.code == CommandErrorCode::not_found));
            BOOST_TEST(
                (response.error.code != CommandErrorCode::storage_failure));
            BOOST_TEST(response.error.message == "table not found");
            BOOST_TEST(response.error.message.find(name)
                       == std::string::npos);
        }
    }

    // The empty string passes the required-field check (present, non-null) and
    // likewise resolves to the same not_found as every other miss.
    const CommandResponse empty =
        runtime.registry->execute(user_caller(), db_rows(""));
    BOOST_TEST(!empty.ok);
    BOOST_TEST((empty.error.code == CommandErrorCode::not_found));
    BOOST_TEST((empty.error.code != CommandErrorCode::storage_failure));
    BOOST_TEST(empty.error.message == "table not found");
}

// Type: unit (no existence oracle: forbidden name == unknown name, T-51).
// Scenario: db.rows for "users" (a plausible real-but-forbidden table) and for
//   "nope" (a name that names nothing) are issued with the same request id. The
//   two serialized response envelopes are byte-identical, so a caller cannot
//   tell a table that exists-but-is-not-exposed from one that does not exist.
// Boundaries: the catalog find() returns the same nullptr for both, and the
//   handler builds a response that depends on neither name; equality is checked
//   on the full serialized envelope, not just the error code.
// On failure first check: did the handler start distinguishing a known-but-
//   forbidden name (e.g. a different message or code), leaking an oracle?
BOOST_AUTO_TEST_CASE(not_found_identical_for_unknown_and_forbidden)
{
    LinkBackend::reset();
    ServerConfig server;
    LinkBackendHandle backend;
    const CommandRuntime runtime = build_command_runtime(server, backend);

    const CommandResponse forbidden =
        runtime.registry->execute(user_caller(), db_rows("users"));
    const CommandResponse unknown =
        runtime.registry->execute(user_caller(), db_rows("nope"));

    BOOST_TEST(!forbidden.ok);
    BOOST_TEST(!unknown.ok);
    BOOST_TEST((forbidden.error.code == CommandErrorCode::not_found));
    BOOST_TEST((unknown.error.code == CommandErrorCode::not_found));

    // The whole wire envelope is identical: same id, ok, code, and message.
    BOOST_TEST(serialize_response(forbidden) == serialize_response(unknown));
}
