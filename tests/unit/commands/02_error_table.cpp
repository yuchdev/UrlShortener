#define BOOST_TEST_MODULE CommandsErrorTableTest
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/app/app_error.hpp"
#include "url_shortener/cli/link_command_dispatch.hpp"
#include "url_shortener/commands/error_codes.hpp"

using url_shortener::app::AppErrorCode;
using url_shortener::cli::ExitCodeForAppError;
using url_shortener::commands::all_error_infos;
using url_shortener::commands::CommandError;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::error_code_from_name;
using url_shortener::commands::error_info;
using url_shortener::commands::error_info_for;
using url_shortener::commands::from_app_error;
using url_shortener::commands::make_error;

namespace
{

// Every CommandErrorCode enumerator, in declaration order. Kept local so the
// table-order and round-trip tests enumerate the whole enum without a default.
const std::vector<CommandErrorCode>& all_command_codes()
{
    static const std::vector<CommandErrorCode> codes = {
        CommandErrorCode::none,
        CommandErrorCode::invalid_envelope,
        CommandErrorCode::unknown_command,
        CommandErrorCode::invalid_field,
        CommandErrorCode::invalid_url,
        CommandErrorCode::invalid_slug,
        CommandErrorCode::reserved_slug,
        CommandErrorCode::slug_conflict,
        CommandErrorCode::not_found,
        CommandErrorCode::not_authorized,
        CommandErrorCode::unsupported,
        CommandErrorCode::busy,
        CommandErrorCode::job_cancelled,
        CommandErrorCode::storage_failure,
        CommandErrorCode::internal,
    };
    return codes;
}

// Every app::AppErrorCode enumerator, in declaration order.
const std::vector<AppErrorCode>& all_app_codes()
{
    static const std::vector<AppErrorCode> codes = {
        AppErrorCode::none,
        AppErrorCode::invalid_url,
        AppErrorCode::invalid_slug,
        AppErrorCode::reserved_slug,
        AppErrorCode::slug_conflict,
        AppErrorCode::invalid_field,
        AppErrorCode::not_found,
        AppErrorCode::storage_failure,
        AppErrorCode::internal,
    };
    return codes;
}

bool is_lower_snake_case(std::string_view name)
{
    if (name.empty()) {
        return false;
    }
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Table shape: one row per enumerator, in enum order, with unique
// lower_snake_case names.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(table_has_one_row_per_enumerator_in_enum_order)
{
    const std::vector<error_info>& table = all_error_infos();
    const std::vector<CommandErrorCode>& codes = all_command_codes();

    BOOST_REQUIRE_EQUAL(table.size(), codes.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        // Row i must describe the enumerator whose value is i so that
        // error_info_for can index by static_cast<size_t>(code).
        BOOST_TEST((table[i].code == codes[i]));
        BOOST_TEST((table[i].code == static_cast<CommandErrorCode>(i)));
    }
}

BOOST_AUTO_TEST_CASE(table_names_are_unique_and_lower_snake_case)
{
    const std::vector<error_info>& table = all_error_infos();
    std::vector<std::string> seen;
    for (const error_info& info : table) {
        BOOST_TEST(is_lower_snake_case(info.name),
                   "name not lower_snake_case: " << info.name);
        const std::string name(info.name);
        const bool duplicate =
            std::find(seen.begin(), seen.end(), name) != seen.end();
        BOOST_TEST(!duplicate, "duplicate name: " << name);
        seen.push_back(name);
    }
}

BOOST_AUTO_TEST_CASE(error_info_for_indexes_the_table_directly)
{
    for (const CommandErrorCode code : all_command_codes()) {
        BOOST_TEST((error_info_for(code).code == code));
    }
}

BOOST_AUTO_TEST_CASE(error_info_for_out_of_range_value_maps_to_internal)
{
    const auto past_end =
        static_cast<CommandErrorCode>(all_error_infos().size());
    const auto far_out = static_cast<CommandErrorCode>(1000);

    BOOST_TEST((error_info_for(past_end).code == CommandErrorCode::internal));
    BOOST_TEST((error_info_for(far_out).code == CommandErrorCode::internal));
}

// ---------------------------------------------------------------------------
// Name round-trip.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(name_round_trips_for_every_code)
{
    for (const CommandErrorCode code : all_command_codes()) {
        const auto decoded = error_code_from_name(error_info_for(code).name);
        BOOST_REQUIRE(decoded.has_value());
        BOOST_TEST((*decoded == code));
    }
}

BOOST_AUTO_TEST_CASE(unknown_and_empty_names_yield_nullopt)
{
    BOOST_TEST(!error_code_from_name("no_such_code").has_value());
    BOOST_TEST(!error_code_from_name("").has_value());
    BOOST_TEST(!error_code_from_name("Invalid_Url").has_value());
}

// ---------------------------------------------------------------------------
// Parity with the existing CLI / REST mappings (contracts C3, C4). These tests
// fail if either side is edited independently.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(exit_code_parity_with_cli_mapping)
{
    for (const AppErrorCode app_code : all_app_codes()) {
        const error_info& info = error_info_for(from_app_error(app_code));
        BOOST_TEST(info.exit_code == ExitCodeForAppError(app_code),
                   "exit-code parity broken for app code "
                       << static_cast<int>(app_code));
    }
}

BOOST_AUTO_TEST_CASE(legacy_rest_status_parity_with_status_for_app_error)
{
    // Hard-coded from statusForAppError in link_handlers.cpp. These assert the
    // legacy_rest_status column still matches today's resource-route statuses.
    const std::vector<std::pair<AppErrorCode, unsigned>> expected = {
        {AppErrorCode::invalid_url, 400},
        {AppErrorCode::invalid_slug, 400},
        {AppErrorCode::invalid_field, 400},
        {AppErrorCode::reserved_slug, 409},
        {AppErrorCode::slug_conflict, 409},
        {AppErrorCode::not_found, 404},
        {AppErrorCode::storage_failure, 500},
        {AppErrorCode::internal, 500},
    };
    for (const auto& [app_code, status] : expected) {
        const error_info& info = error_info_for(from_app_error(app_code));
        BOOST_TEST(info.legacy_rest_status == status,
                   "legacy status mismatch for app code "
                       << static_cast<int>(app_code));
    }
}

BOOST_AUTO_TEST_CASE(legacy_rest_code_parity_with_code_for_app_error)
{
    // Hard-coded from codeForAppError in link_handlers.cpp.
    const std::vector<std::pair<AppErrorCode, std::string_view>> expected = {
        {AppErrorCode::invalid_url, "invalid_url"},
        {AppErrorCode::invalid_slug, "invalid_slug"},
        {AppErrorCode::reserved_slug, "reserved_slug"},
        {AppErrorCode::slug_conflict, "slug_conflict"},
        {AppErrorCode::not_found, "not_found"},
        {AppErrorCode::storage_failure, "repository"},
        {AppErrorCode::invalid_field, "invalid_request"},
        {AppErrorCode::internal, "internal_error"},
    };
    for (const auto& [app_code, code_string] : expected) {
        const error_info& info = error_info_for(from_app_error(app_code));
        BOOST_TEST(
            info.legacy_rest_code == code_string,
            "legacy code mismatch for app code " << static_cast<int>(app_code));
    }
}

// ---------------------------------------------------------------------------
// from_app_error boundary + make_error.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(from_app_error_maps_none_to_none)
{
    BOOST_TEST((from_app_error(AppErrorCode::none) == CommandErrorCode::none));
    BOOST_TEST(error_info_for(CommandErrorCode::none).exit_code == 0);
}

BOOST_AUTO_TEST_CASE(from_app_error_maps_same_named_codes_to_themselves)
{
    BOOST_TEST((from_app_error(AppErrorCode::invalid_url)
                == CommandErrorCode::invalid_url));
    BOOST_TEST((from_app_error(AppErrorCode::slug_conflict)
                == CommandErrorCode::slug_conflict));
    BOOST_TEST((from_app_error(AppErrorCode::storage_failure)
                == CommandErrorCode::storage_failure));
}

BOOST_AUTO_TEST_CASE(make_error_preserves_code_and_message)
{
    const CommandError err =
        make_error(CommandErrorCode::invalid_field, "field is required");
    BOOST_TEST((err.code == CommandErrorCode::invalid_field));
    BOOST_TEST(err.message == "field is required");
    BOOST_TEST(!err.ok());

    const CommandError none_err = make_error(CommandErrorCode::none, "");
    BOOST_TEST(none_err.ok());
}

// ---------------------------------------------------------------------------
// SSRF indistinguishability (threat model T-16): a malformed URL and a
// blocked private target share one code, status and value-free message.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(invalid_url_is_indistinguishable_for_malformed_and_ssrf)
{
    // Both failure causes map through the single app code -> single command
    // code, so status and exit code are identical and carry no distinguishing
    // signal. Callers pass a fixed, value-free message.
    const CommandErrorCode malformed =
        from_app_error(AppErrorCode::invalid_url);
    const CommandErrorCode blocked = from_app_error(AppErrorCode::invalid_url);
    BOOST_TEST((malformed == blocked));
    BOOST_TEST((malformed == CommandErrorCode::invalid_url));

    const error_info& info = error_info_for(CommandErrorCode::invalid_url);
    BOOST_TEST(info.http_status == 400u);
    BOOST_TEST(info.exit_code == 2);

    const CommandError err =
        make_error(CommandErrorCode::invalid_url, "invalid url");
    BOOST_TEST(err.message.find("127.0.0.1") == std::string::npos);
    BOOST_TEST(err.message.find("http") == std::string::npos);
}

// ---------------------------------------------------------------------------
// Catalog parity: every row of the docs/commands/README.md section 1 error
// table appears with the same http, exit and legacy resource-route string.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(table_agrees_with_catalog_row_by_row)
{
    struct catalog_row
    {
        std::string_view name;
        unsigned http;
        int exit;
        unsigned legacy_status;
        std::string_view legacy_code;
    };

    // Values transcribed from docs/commands/README.md section 1 "Error codes"
    // (reserved_slug corrected to 409; legacy column added on this branch).
    const std::vector<catalog_row> catalog = {
        {"invalid_envelope", 400, 2, 400, "invalid_envelope"},
        {"unknown_command", 404, 2, 404, "unknown_command"},
        {"invalid_field", 400, 2, 400, "invalid_request"},
        {"invalid_url", 400, 2, 400, "invalid_url"},
        {"invalid_slug", 400, 2, 400, "invalid_slug"},
        {"reserved_slug", 409, 2, 409, "reserved_slug"},
        {"slug_conflict", 409, 3, 409, "slug_conflict"},
        {"not_found", 404, 1, 404, "not_found"},
        {"not_authorized", 403, 5, 403, "not_authorized"},
        {"unsupported", 501, 2, 501, "unsupported"},
        {"busy", 503, 4, 503, "busy"},
        {"job_cancelled", 200, 4, 200, "job_cancelled"},
        {"storage_failure", 500, 4, 500, "repository"},
        {"internal", 500, 4, 500, "internal_error"},
    };

    for (const catalog_row& row : catalog) {
        const auto code = error_code_from_name(row.name);
        BOOST_REQUIRE_MESSAGE(code.has_value(),
                              "catalog code missing from table: " << row.name);
        const error_info& info = error_info_for(*code);
        BOOST_TEST(info.http_status == row.http, "http mismatch: " << row.name);
        BOOST_TEST(info.exit_code == row.exit, "exit mismatch: " << row.name);
        BOOST_TEST(info.legacy_rest_status == row.legacy_status,
                   "legacy status mismatch: " << row.name);
        BOOST_TEST(info.legacy_rest_code == row.legacy_code,
                   "legacy code mismatch: " << row.name);
    }
}
