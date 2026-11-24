/**
 * @file 70_db_table_allowlist_test.cpp
 * @brief Gap cases for the `db.*` table allowlist (milestone contract C6,
 * threat model T-51): `db.rows` resolves `table` only through the positive
 * allowlist
 * (`app::TableCatalog::find`, `db_commands.cpp` `handle_db_rows`), so a
 * hostile, case-shifted, injection-shaped, path-shaped or empty name all
 * collapse to one identical `not_found` and never a `storage_failure`, and an
 * unknown name is indistinguishable from a real-but-forbidden one (no existence
 * oracle).
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
#include "url_shortener/core/utils.hpp"

using url_shortener::jsonString;
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
// (the no-oracle comparison depends on the id being identical). The table value
// is JSON-encoded with jsonString, so a name carrying a double quote, a control
// byte (NUL) or multi-byte UTF-8 (a homoglyph) is delivered to the parser as
// the exact intended byte string, not a malformed envelope.
CommandRequest db_rows(const std::string& table_value)
{
    CommandRequest req;
    req.id = "client-1";
    req.command = "db.rows";
    req.args_json = R"({"table":)" + jsonString(table_value) + "}";
    return req;
}

// Cyrillic small letter "а" (U+0430, UTF-8 D0 B0) standing in for the Latin "i"
// of "links": a visual homoglyph whose bytes differ, so a byte-exact allowlist
// must miss it.
const std::string k_homoglyph_links = std::string("l") + "\xD0\xB0" + "nks";

// "links" with an embedded NUL then a trailing byte, length-explicit so the NUL
// is retained (a C-string literal would truncate at the NUL).
const std::string k_nul_links("links\0x", 7);

}  // namespace

// Type: unit (every non-allowlisted name -> one identical not_found, T-51).
// Scenario: auth tables, generic/schema catalog tables, a cross-schema
//   qualified name, an injection-shaped name, a case variant, a trailing-space
//   variant, a path-shaped name, a Unicode homoglyph of "links", the four SQL
//   identifier-quoting forms ("links", `links`, 'links', [links]), and the
//   empty string are each resolved through db.rows against the production
//   allowlist (links, click_events, jobs). Every one fails with not_found and
//   the fixed message "table not found", never storage_failure, and never
//   echoes the requested name.
// Boundaries: the catalog find() matches byte-for-byte, so "LINKS", "links ",
//   "../links", the homoglyph and every quoted form are misses; the failure
//   code is a single value across the whole hostile set. A separate block
//   covers the embedded NUL, which is refused earlier (see below).
// On failure first check: did find() start matching loosely/trimming/stripping
//   quotes, did a name reach the backend (storage_failure), or did the message
//   start quoting the argument?
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
        k_homoglyph_links,  // Cyrillic homoglyph of "links"
        "\"links\"",  // SQL double-quoted identifier
        "`links`",  // SQL backtick-quoted identifier
        "'links'",  // single-quoted
        "[links]",  // bracket-quoted
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
            BOOST_TEST(response.error.message.find(name) == std::string::npos);
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

    // An embedded-NUL name is refused one step earlier than the allowlist: the
    // registry's string-arg decoder (`decode_without_nul`, threat model T-03)
    // rejects any value carrying a NUL, so the code is invalid_field, not
    // not_found. This is a stricter, earlier refusal -- the name never reaches
    // find() or the backend, and the field-named message does not echo the
    // bytes. (The no-echo search uses the length-explicit std::string so the
    // NUL and its trailing byte are both searched.)
    const CommandResponse nul =
        runtime.registry->execute(user_caller(), db_rows(k_nul_links));
    BOOST_TEST(!nul.ok);
    BOOST_TEST((nul.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST((nul.error.code != CommandErrorCode::storage_failure));
    BOOST_TEST(nul.error.message.find(k_nul_links) == std::string::npos);
}

// Type: unit (no existence oracle: forbidden name == unknown name, T-51).
// Scenario: db.rows for "users" (a plausible real-but-forbidden table), for
//   "nope" (a name that names nothing) and for a SQL-quoted [links] are issued
//   with the same request id; all three serialized envelopes are
//   byte-identical, so a caller cannot tell a table that
//   exists-but-is-not-exposed from one that does not exist, nor probe a real
//   name by quoting it. A separate pair shows the embedded-NUL refusal
//   (invalid_field) is likewise content-independent: two different NUL-bearing
//   names serialize identically.
// Boundaries: find() returns the same nullptr for every not_found name, and the
//   NUL decoder returns the same field-named invalid_field for any NUL value;
//   equality is checked on the full serialized envelope, not just the code.
// On failure first check: did the handler start distinguishing a known-but-
//   forbidden name (a different message/code), or did a NUL value's bytes reach
//   the error message?
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
    const CommandResponse quoted =
        runtime.registry->execute(user_caller(), db_rows("[links]"));

    BOOST_TEST(!forbidden.ok);
    BOOST_TEST(!unknown.ok);
    BOOST_TEST((forbidden.error.code == CommandErrorCode::not_found));
    BOOST_TEST((unknown.error.code == CommandErrorCode::not_found));
    BOOST_TEST((quoted.error.code == CommandErrorCode::not_found));

    // The whole wire envelope is identical: same id, ok, code, and message.
    BOOST_TEST(serialize_response(forbidden) == serialize_response(unknown));
    BOOST_TEST(serialize_response(quoted) == serialize_response(unknown));

    // Two distinct embedded-NUL names are refused identically (invalid_field,
    // field-named), so the earlier NUL rejection exposes no content oracle.
    const CommandResponse nul_a =
        runtime.registry->execute(user_caller(), db_rows(k_nul_links));
    const CommandResponse nul_b = runtime.registry->execute(
        user_caller(), db_rows(std::string("zzzz\0y", 6)));
    BOOST_TEST((nul_a.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST((nul_b.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(serialize_response(nul_a) == serialize_response(nul_b));
}
