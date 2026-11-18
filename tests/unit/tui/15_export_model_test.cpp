#define BOOST_TEST_MODULE TuiExportModelTest

#include <string>

#include <boost/test/unit_test.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/import_model.hpp>
#include <url_shortener/tui/links_query.hpp>

/**
 * @file 15_export_model_test.cpp
 * @brief Exact-bytes request tests for @ref ExportModel (D7, subtask 06.0/15;
 * FTXUI-free model library).
 *
 * Type: unit (model library, links url_shortener_tui_model).
 * Scenario: to_request emits `args` in catalog order (format, status, q, tag,
 *           campaign, path), always sends status, and omits empty filter terms
 *           and an empty path.
 * Boundaries: csv vs ndjson; filter present vs absent; path present vs absent.
 * On failure first check: ExportModel::to_request in src/tui/import_model.cpp.
 */

namespace tui = url_shortener::tui;
namespace commands = url_shortener::commands;

// ---------------------------------------------------------------------------
// Default form: only format and status (always sent).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(default_form_bytes)
{
    tui::ExportModel model;
    const commands::CommandRequest req = model.to_request("tui-1");
    BOOST_CHECK_EQUAL(req.command, "link.export");
    BOOST_CHECK_EQUAL(req.args_json, "{\"format\":\"csv\",\"status\":\"all\"}");
}

// ---------------------------------------------------------------------------
// Format toggles to ndjson.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(ndjson_format)
{
    tui::ExportModel model;
    model.set_format(tui::ImportFormat::ndjson);
    const commands::CommandRequest req = model.to_request("tui-2");
    BOOST_CHECK_EQUAL(req.args_json,
                      "{\"format\":\"ndjson\",\"status\":\"all\"}");
}

// ---------------------------------------------------------------------------
// Filters map to status / q / tag / campaign in catalog order.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(filters_mapped)
{
    tui::ExportModel model;
    tui::LinkListFilter filter;
    filter.status = tui::StatusFilter::active;
    filter.q = "promo";
    filter.tag = "spring";
    filter.campaign = "launch";
    model.set_filter(filter);

    const commands::CommandRequest req = model.to_request("tui-3");
    BOOST_CHECK_EQUAL(
        req.args_json,
        "{\"format\":\"csv\",\"status\":\"active\",\"q\":\"promo\","
        "\"tag\":\"spring\",\"campaign\":\"launch\"}");
}

// ---------------------------------------------------------------------------
// Path is optional and, when present, is the last member.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(path_optional)
{
    tui::ExportModel model;
    const commands::CommandRequest without = model.to_request("tui-4");
    BOOST_CHECK(without.args_json.find("\"path\"") == std::string::npos);

    model.set_path("/tmp/out.csv");
    const commands::CommandRequest with = model.to_request("tui-5");
    BOOST_CHECK_EQUAL(
        with.args_json,
        "{\"format\":\"csv\",\"status\":\"all\",\"path\":\"/tmp/out.csv\"}");
}
