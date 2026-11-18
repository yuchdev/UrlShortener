#define BOOST_TEST_MODULE TuiFormModelRequestTest

#include <cstddef>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/form_model.hpp>

#include "golden.hpp"

/**
 * @file 07_form_model_request_test.cpp
 * @brief Exact-bytes request tests for the FormModel, built from hand-copied
 * catalog descriptors (FTXUI-free model library).
 *
 * Type: unit (model library, links url_shortener_tui_model).
 * Scenario: forms built from `link.create`, `link.update`, `link.list` and
 *           `link.import` descriptors produce `args` bytes that match committed
 *           goldens; a cleared nullable field emits explicit `null`; the dry-run
 *           toggle reaches `options.dry_run` only for commands that support it;
 *           special characters in a slug/URL are JSON-escaped; and a conformance
 *           check fails if the catalog's argument names drift from these
 *           fixtures.
 * Boundaries: PATCH clear (`"expires_at":null`); dry-run honoured vs ignored;
 *           escaping of `"`, `\\`, newline and non-ASCII.
 * On failure first check: the fixture `CommandInfo`s below, `to_args_json` in
 *           `src/tui/form_model.cpp`, or (for the drift case) the catalog table
 *           in `docs/commands/README.md`.
 */

using url_shortener::tui::ArgInfo;
using url_shortener::tui::ArgKind;
using url_shortener::tui::CommandInfo;
using url_shortener::tui::FormModel;
using url_shortener::tui::testing::assert_matches_golden;
using url_shortener::tui::testing::golden_dir;

namespace cmd = url_shortener::commands;

namespace
{

ArgInfo arg(const std::string& name,
            ArgKind kind,
            bool required = false,
            bool nullable = false)
{
    ArgInfo info;
    info.name = name;
    info.kind = kind;
    info.required = required;
    info.nullable = nullable;
    return info;
}

// --- Hand-copied catalog descriptors (docs/commands/README.md section 3). -----

CommandInfo link_create_info()
{
    CommandInfo info;
    info.name = "link.create";
    info.permission = "write";
    info.mutating = true;
    info.supports_dry_run = true;
    ArgInfo redirect = arg("redirect_type", ArgKind::enumeration);
    redirect.enum_values = {"temporary", "permanent"};
    ArgInfo campaign = arg("campaign", ArgKind::object, false, true);
    campaign.members = {"name", "source", "medium", "term", "content", "id"};
    info.args = {arg("target_url", ArgKind::string, true),
                 arg("slug", ArgKind::string),
                 redirect,
                 arg("expires_at", ArgKind::timestamp, false, true),
                 arg("enabled", ArgKind::boolean),
                 arg("tags", ArgKind::string_list),
                 arg("metadata", ArgKind::string_map),
                 campaign};
    return info;
}

CommandInfo link_update_info()
{
    CommandInfo info;
    info.name = "link.update";
    info.permission = "write";
    info.mutating = true;
    info.supports_dry_run = true;
    ArgInfo campaign = arg("campaign", ArgKind::object, false, true);
    campaign.members = {"name", "source", "medium", "term", "content", "id"};
    info.args = {arg("slug", ArgKind::string, true),
                 arg("enabled", ArgKind::boolean),
                 arg("expires_at", ArgKind::timestamp, false, true),
                 arg("tags", ArgKind::string_list),
                 arg("metadata", ArgKind::string_map),
                 campaign};
    return info;
}

CommandInfo link_list_info()
{
    CommandInfo info;
    info.name = "link.list";
    info.permission = "read";
    ArgInfo status = arg("status", ArgKind::enumeration);
    status.enum_values = {"all", "active", "disabled", "expired", "deleted"};
    ArgInfo sort = arg("sort", ArgKind::enumeration);
    sort.enum_values = {"created_at", "slug", "updated_at"};
    ArgInfo order = arg("order", ArgKind::enumeration);
    order.enum_values = {"asc", "desc"};
    info.args = {status,
                 arg("q", ArgKind::string),
                 arg("tag", ArgKind::string),
                 arg("campaign", ArgKind::string),
                 sort,
                 order,
                 arg("offset", ArgKind::integer),
                 arg("limit", ArgKind::integer)};
    return info;
}

CommandInfo link_import_info()
{
    CommandInfo info;
    info.name = "link.import";
    info.permission = "write";
    info.mutating = true;
    info.supports_dry_run = true;
    info.is_job = true;
    ArgInfo format = arg("format", ArgKind::enumeration, true);
    format.enum_values = {"csv", "ndjson"};
    ArgInfo on_conflict = arg("on_conflict", ArgKind::enumeration);
    on_conflict.enum_values = {"skip", "fail", "overwrite"};
    info.args = {format,
                 arg("data", ArgKind::string),
                 arg("path", ArgKind::string),
                 on_conflict};
    return info;
}

std::size_t index_of(const FormModel& model, const std::string& name)
{
    const std::vector<url_shortener::tui::FormField>& fields = model.fields();
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].spec.name == name) {
            return i;
        }
    }
    BOOST_FAIL("fixture has no field named " << name);
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// link.create: a representative filled form matches the golden bytes.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(link_create_request_matches_golden)
{
    FormModel model(link_create_info());
    model.set_text(index_of(model, "target_url"), "https://example.com/docs");
    model.set_text(index_of(model, "slug"), "docs");
    model.set_choice(index_of(model, "redirect_type"), 1);  // permanent
    model.set_text(index_of(model, "tags"), "a, b");
    model.set_text(index_of(model, "metadata"), "k=v");

    BOOST_CHECK(model.validate());
    assert_matches_golden(model.to_args_json(), "07_request_link_create.json");
}

// ---------------------------------------------------------------------------
// link.update: clearing a nullable field emits explicit null (PATCH).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(link_update_clear_request_matches_golden)
{
    FormModel model(link_update_info());
    model.set_text(index_of(model, "slug"), "promo");
    BOOST_CHECK(model.clear_field(index_of(model, "expires_at")));

    assert_matches_golden(model.to_args_json(),
                          "07_request_link_update_clear.json");
}

// ---------------------------------------------------------------------------
// link.list: only the touched enum is sent; untouched defaults are omitted.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(link_list_request_matches_golden)
{
    FormModel model(link_list_info());
    model.set_choice(index_of(model, "status"), 0);  // all

    assert_matches_golden(model.to_args_json(), "07_request_link_list_all.json");
}

// ---------------------------------------------------------------------------
// dry_run reaches options.dry_run only for a command that supports it.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(dry_run_honoured_only_when_supported)
{
    FormModel creator(link_create_info());
    creator.set_text(index_of(creator, "target_url"), "https://x");
    creator.set_dry_run(true);
    const cmd::CommandRequest create_req = creator.to_request("tui-1");
    BOOST_CHECK(create_req.options.dry_run);
    BOOST_CHECK_EQUAL(create_req.id, "tui-1");
    BOOST_CHECK_EQUAL(create_req.command, "link.create");

    FormModel lister(link_list_info());
    lister.set_dry_run(true);  // link.list does not support dry-run
    const cmd::CommandRequest list_req = lister.to_request("tui-2");
    BOOST_CHECK(!list_req.options.dry_run);
}

// ---------------------------------------------------------------------------
// Special characters in slug/URL are JSON-escaped for every byte.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(special_characters_escaped)
{
    FormModel model(link_create_info());
    model.set_text(index_of(model, "target_url"),
                   std::string("https://x/\"a\\b\nc"));
    model.set_text(index_of(model, "slug"),
                   std::string("sl\xc3\xa9ug"));  // non-ASCII (é)
    const std::string args = model.to_args_json();
    BOOST_CHECK_EQUAL(
        args,
        "{\"target_url\":\"https://x/\\\"a\\\\b\\nc\",\"slug\":"
        "\"sl\xc3\xa9ug\"}");
}

// ---------------------------------------------------------------------------
// Conformance: the fixtures' argument names must match the catalog tables, so
// a drift in docs/commands/README.md fails loudly here.
// ---------------------------------------------------------------------------
namespace
{

std::string read_catalog()
{
    const std::string path = golden_dir() + "/../../../../docs/commands/README.md";
    std::ifstream in(path, std::ios::binary);
    BOOST_REQUIRE_MESSAGE(in.is_open(), "cannot open catalog: " << path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// Extracts the argument names of one command's table from the catalog text.
/// Finds the first `#### \`<command>\`` heading and reads the markdown table
/// that follows, collecting the first back-ticked token of each table row.
std::set<std::string> catalog_arg_names(const std::string& catalog,
                                        const std::string& command)
{
    const std::string heading = "#### `" + command + "`";
    std::set<std::string> names;
    std::istringstream stream(catalog);
    std::string line;
    bool in_section = false;
    bool table_started = false;
    while (std::getline(stream, line)) {
        if (!in_section) {
            if (line.rfind(heading, 0) == 0) {
                in_section = true;
            }
            continue;
        }
        if (!line.empty() && line[0] == '|') {
            const std::size_t open = line.find('`');
            if (open != std::string::npos) {
                const std::size_t close = line.find('`', open + 1);
                if (close != std::string::npos && close > open + 1) {
                    names.insert(line.substr(open + 1, close - open - 1));
                    table_started = true;
                }
            }
        }
        else if (table_started) {
            break;  // the table ended
        }
    }
    return names;
}

std::set<std::string> fixture_arg_names(const CommandInfo& info)
{
    std::set<std::string> names;
    for (const ArgInfo& a : info.args) {
        names.insert(a.name);
    }
    return names;
}

}  // namespace

BOOST_AUTO_TEST_CASE(fixtures_match_catalog_arg_names)
{
    const std::string catalog = read_catalog();
    BOOST_CHECK(catalog_arg_names(catalog, "link.create")
                == fixture_arg_names(link_create_info()));
    BOOST_CHECK(catalog_arg_names(catalog, "link.update")
                == fixture_arg_names(link_update_info()));
    BOOST_CHECK(catalog_arg_names(catalog, "link.list")
                == fixture_arg_names(link_list_info()));
    BOOST_CHECK(catalog_arg_names(catalog, "link.import")
                == fixture_arg_names(link_import_info()));
}
