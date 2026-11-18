#define BOOST_TEST_MODULE TuiLinksModelTest

#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/tui/links_model.hpp>
#include <url_shortener/tui/table_model.hpp>

/**
 * @file 12_links_model_test.cpp
 * @brief Availability-matrix and row-mapping tests for the Links model
 * (FTXUI-free model library, subtask 06.0/12).
 *
 * Type: unit (model library, links url_shortener_tui_model).
 * Scenario: the 4 link statuses x 8 actions x read-only on/off availability
 *           matrix matches the D3 rules (read actions always on; writes gated
 * by read-only and by status: toggle/edit/remove off for deleted, restore only
 * for deleted); the toggle command maps enable/disable by status; a full and a
 * minimal LinkView JSON map to the D3 cells (missing stats / expires render
 * `-`). Boundaries: read vs write actions; read-only on/off; deleted vs live
 * status. On failure first check: availability / toggle_command /
 * row_from_link_json in src/tui/links_model.cpp.
 */

using url_shortener::tui::ActionAvailability;
using url_shortener::tui::ColumnDef;
using url_shortener::tui::LinkAction;
using url_shortener::tui::LinksModel;

namespace
{

/// The four link statuses the row column can carry.
const std::vector<std::string> k_statuses = {
    "active", "disabled", "expired", "deleted"};

/// Whether an action is a read action (always available).
bool is_read_action(LinkAction action)
{
    return action == LinkAction::detail || action == LinkAction::preview
        || action == LinkAction::stats;
}

/// The expected availability of a write action by status in a full session.
bool expected_write_enabled(LinkAction action, const std::string& status)
{
    const bool deleted = status == "deleted";
    switch (action) {
        case LinkAction::create:
            return true;  // independent of selection
        case LinkAction::edit:
        case LinkAction::toggle:
        case LinkAction::remove:
            return !deleted;
        case LinkAction::restore:
            return deleted;
        default:
            return false;
    }
}

const std::vector<LinkAction> k_actions = {LinkAction::detail,
                                           LinkAction::create,
                                           LinkAction::edit,
                                           LinkAction::toggle,
                                           LinkAction::remove,
                                           LinkAction::restore,
                                           LinkAction::preview,
                                           LinkAction::stats};

}  // namespace

// ---------------------------------------------------------------------------
// Full session: the 8 actions x 4 statuses availability matrix.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(availability_matrix_full_session)
{
    LinksModel model(/*read_only=*/false);
    for (const std::string& status : k_statuses) {
        for (LinkAction action : k_actions) {
            const ActionAvailability avail = model.availability(action, status);
            const bool expected = is_read_action(action)
                ? true
                : expected_write_enabled(action, status);
            BOOST_TEST(
                avail.enabled == expected,
                "status=" << status << " action=" << static_cast<int>(action));
            if (!avail.enabled) {
                BOOST_TEST(!avail.reason.empty());
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Read-only session: every write action is disabled with the read-only reason;
// read actions stay enabled.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(availability_matrix_read_only_session)
{
    LinksModel model(/*read_only=*/true);
    for (const std::string& status : k_statuses) {
        for (LinkAction action : k_actions) {
            const ActionAvailability avail = model.availability(action, status);
            if (is_read_action(action)) {
                BOOST_TEST(avail.enabled,
                           "read action disabled for status=" << status);
            }
            else {
                BOOST_TEST(
                    !avail.enabled,
                    "write action enabled in read-only for status=" << status);
                BOOST_TEST(avail.reason == "read-only session");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Toggle maps enable/disable by status; deleted/unknown yields no command.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(toggle_command_by_status)
{
    LinksModel model(/*read_only=*/false);
    BOOST_TEST(model.toggle_command("active") == "link.disable");
    BOOST_TEST(model.toggle_command("expired") == "link.disable");
    BOOST_TEST(model.toggle_command("disabled") == "link.enable");
    BOOST_TEST(model.toggle_command("deleted").empty());
    BOOST_TEST(model.toggle_command("").empty());
}

// ---------------------------------------------------------------------------
// No selection: row-scoped writes are disabled, create and reads stay enabled.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(no_selection_disables_row_writes)
{
    LinksModel model(/*read_only=*/false);
    BOOST_TEST(!model.availability(LinkAction::edit, "").enabled);
    BOOST_TEST(!model.availability(LinkAction::toggle, "").enabled);
    BOOST_TEST(!model.availability(LinkAction::remove, "").enabled);
    BOOST_TEST(model.availability(LinkAction::create, "").enabled);
    BOOST_TEST(model.availability(LinkAction::detail, "").enabled);
}

// ---------------------------------------------------------------------------
// The D3 columns and their JSON keys.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(columns_and_keys)
{
    LinksModel model(/*read_only=*/false);
    const std::vector<ColumnDef> columns = model.columns();
    BOOST_REQUIRE_EQUAL(columns.size(), 5u);
    BOOST_TEST(columns[0].title == "SLUG");
    BOOST_TEST(columns[1].title == "TARGET URL");
    BOOST_TEST(columns[2].title == "STATUS");
    BOOST_TEST(columns[3].title == "CLICKS");
    BOOST_TEST(columns[4].title == "EXPIRES");
    BOOST_TEST(columns[3].right_align);  // CLICKS is numeric

    const std::vector<std::string> keys = model.column_keys();
    const std::vector<std::string> expected = {
        "slug", "url", "status", "stats.total_redirects", "expires_at"};
    BOOST_TEST(keys == expected);
}

// ---------------------------------------------------------------------------
// A full LinkView JSON maps to the five D3 cells (expires has no source -> -).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(row_from_full_link_json)
{
    LinksModel model(/*read_only=*/false);
    const std::string json =
        R"({"id":"l-1","slug":"docs",)"
        R"("url":"https://example.com/docs/getting-started",)"
        R"("short_url":"https://s/docs","created_at":"2026-01-01T00:00:00Z",)"
        R"("updated_at":"2026-01-02T00:00:00Z","status":"active",)"
        R"("redirect_type":"temporary","tags":["a"],"metadata":{"k":"v"},)"
        R"("campaign":{"name":"spring"},)"
        R"("stats":{"total_redirects":12044,"redirects_24h":5,)"
        R"("redirects_7d":40,"last_accessed_at":"2026-01-02T00:00:00Z"}})";
    const std::vector<std::string> row = model.row_from_link_json(json);
    const std::vector<std::string> expected = {
        "docs",
        "https://example.com/docs/getting-started",
        "active",
        "12044",
        "-"};
    BOOST_TEST(row == expected);
}

// ---------------------------------------------------------------------------
// A minimal LinkView JSON (no stats, no expires) renders `-` for the missing
// clicks and expires cells.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(row_from_minimal_link_json)
{
    LinksModel model(/*read_only=*/false);
    const std::string json =
        R"({"slug":"promo","url":"https://x","status":"disabled"})";
    const std::vector<std::string> row = model.row_from_link_json(json);
    const std::vector<std::string> expected = {
        "promo", "https://x", "disabled", "-", "-"};
    BOOST_TEST(row == expected);
}
