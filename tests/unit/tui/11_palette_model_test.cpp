#define BOOST_TEST_MODULE TuiPaletteModelTest

#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/palette_model.hpp>

/**
 * @file 11_palette_model_test.cpp
 * @brief Ranking/filter tests for the Command Palette model (D9, 06.0/11).
 *
 * Type: unit (pure model, links url_shortener_tui_model, no FTXUI).
 * Scenario: an empty query lists every command in name order; a `link.` prefix
 *           shows only link commands; `lst` subsequence-matches `link.list`; an
 *           unknown query yields nothing; a read-only session disables
 *           write/migrate rows with a reason; a synthetic command appears with
 *           no code change (the D9 "zero TUI code" claim); equal scores keep a
 *           stable name order; the `(confirm)`/`(jobs)` tags are derived, not
 *           hard-coded.
 * Boundaries: empty vs non-empty query; exact vs prefix vs segment vs
 *           subsequence vs summary match; read-only vs full session.
 * On failure first check: PaletteModel::score tiers and the tag derivation
 *           (needs_typed_confirm via ConfirmPolicy) in palette_model.cpp.
 */

namespace tui = url_shortener::tui;

namespace
{

tui::CommandInfo cmd(const std::string& name,
                     const std::string& summary,
                     const std::string& permission,
                     bool mutating = false,
                     bool is_job = false)
{
    tui::CommandInfo info;
    info.name = name;
    info.summary = summary;
    info.permission = permission;
    info.mutating = mutating;
    info.is_job = is_job;
    return info;
}

/// A representative catalog in arbitrary order (the model must sort it).
std::vector<tui::CommandInfo> catalog()
{
    return {
        cmd("link.update", "Partially update a link", "write", true),
        cmd("link.create", "Create a short link", "write", true),
        cmd("link.get", "Fetch a link by slug or id", "read"),
        cmd("link.list", "List links (filter, page)", "read"),
        cmd("link.delete", "Soft-delete a link", "write", true),
        cmd("link.import", "Import pairs", "write", true, true),
        cmd("link.export", "Export pairs", "read", false, true),
        cmd("db.tables", "List logical tables", "read"),
        cmd("system.info", "Server info", "read"),
    };
}

const tui::PaletteEntry* find(const std::vector<tui::PaletteEntry>& entries,
                              const std::string& name)
{
    for (const tui::PaletteEntry& e : entries) {
        if (e.info != nullptr && e.info->name == name) {
            return &e;
        }
    }
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// An empty query lists every command, sorted by name.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(empty_query_lists_all_sorted)
{
    tui::PaletteModel model(catalog(), /*read_only=*/false);
    const std::vector<tui::PaletteEntry>& entries = model.entries();
    BOOST_REQUIRE_EQUAL(entries.size(), 9u);
    for (std::size_t i = 1; i < entries.size(); ++i) {
        const std::string prev = entries[i - 1].info->name;
        const std::string cur = entries[i].info->name;
        // Extra parens: BOOST_TEST's auto-decomposition mis-handles
        // std::string::operator< (it compares by length), so disable it here.
        BOOST_CHECK_MESSAGE(prev < cur,
                            prev << " should sort before " << cur);
    }
}

// ---------------------------------------------------------------------------
// A `link.` prefix shows only the link.* commands.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(link_prefix_shows_only_link_commands)
{
    tui::PaletteModel model(catalog(), false);
    model.set_query("link.");
    const std::vector<tui::PaletteEntry>& entries = model.entries();
    BOOST_REQUIRE(!entries.empty());
    for (const tui::PaletteEntry& e : entries) {
        BOOST_TEST(e.info->name.rfind("link.", 0) == 0u);
    }
    BOOST_TEST(find(entries, "db.tables") == nullptr);
    BOOST_TEST(find(entries, "system.info") == nullptr);
}

// ---------------------------------------------------------------------------
// The leading palette prefix `:` is stripped before matching.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(leading_colon_is_stripped)
{
    tui::PaletteModel model(catalog(), false);
    model.set_query(":link.");
    BOOST_TEST(model.query() == "link.");
    for (const tui::PaletteEntry& e : model.entries()) {
        BOOST_TEST(e.info->name.rfind("link.", 0) == 0u);
    }
}

// ---------------------------------------------------------------------------
// `lst` subsequence-matches `link.list`.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(subsequence_matches_link_list)
{
    tui::PaletteModel model(catalog(), false);
    model.set_query("lst");
    BOOST_TEST(find(model.entries(), "link.list") != nullptr);
}

// ---------------------------------------------------------------------------
// An unknown query yields an empty list.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(unknown_query_is_empty)
{
    tui::PaletteModel model(catalog(), false);
    model.set_query("zzzznope");
    BOOST_TEST(model.entries().empty());
    BOOST_TEST(model.selected() == static_cast<const tui::PaletteEntry*>(
                                       nullptr));
}

// ---------------------------------------------------------------------------
// A read-only session disables write/migrate commands with a reason; read
// commands stay enabled.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(read_only_disables_write_commands)
{
    tui::PaletteModel model(catalog(), /*read_only=*/true);
    const std::vector<tui::PaletteEntry>& entries = model.entries();

    const tui::PaletteEntry* create = find(entries, "link.create");
    BOOST_REQUIRE(create != nullptr);
    BOOST_TEST(!create->enabled);
    BOOST_TEST(create->disabled_reason == "not_authorized (read-only session)");

    const tui::PaletteEntry* get = find(entries, "link.get");
    BOOST_REQUIRE(get != nullptr);
    BOOST_TEST(get->enabled);
    BOOST_TEST(get->disabled_reason.empty());
}

// ---------------------------------------------------------------------------
// The tag carries the base permission plus derived (confirm)/(jobs) markers -
// never a hard-coded command list (contract C1/C8).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(tags_carry_confirm_and_jobs_markers)
{
    tui::PaletteModel model(catalog(), false);
    const std::vector<tui::PaletteEntry>& entries = model.entries();

    BOOST_TEST(find(entries, "link.get")->tag == "read");
    BOOST_TEST(find(entries, "link.create")->tag == "write");
    BOOST_TEST(find(entries, "link.delete")->tag == "write (confirm)");
    BOOST_TEST(find(entries, "link.import")->tag == "write (jobs)");
    BOOST_TEST(find(entries, "link.export")->tag == "read (jobs)");
}

// ---------------------------------------------------------------------------
// A command added to the catalog appears with no palette code change (D9 "zero
// TUI code").
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(synthetic_command_appears)
{
    std::vector<tui::CommandInfo> commands = catalog();
    commands.push_back(cmd("widget.frobnicate", "Frobnicate a widget", "read"));
    tui::PaletteModel model(commands, false);
    BOOST_TEST(find(model.entries(), "widget.frobnicate") != nullptr);

    model.set_query("frob");
    BOOST_TEST(find(model.entries(), "widget.frobnicate") != nullptr);
}

// ---------------------------------------------------------------------------
// Equal scores keep a stable, deterministic name order, and selection moves
// within the ranked list.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(stable_order_and_selection)
{
    tui::PaletteModel model(catalog(), false);  // empty query -> equal scores
    const std::vector<tui::PaletteEntry>& entries = model.entries();
    BOOST_REQUIRE(entries.size() >= 2u);
    BOOST_TEST(entries.front().info->name == "db.tables");  // first by name

    BOOST_REQUIRE(model.selected() != nullptr);
    BOOST_TEST(model.selected_index() == 0u);
    model.select_next();
    BOOST_TEST(model.selected_index() == 1u);
    model.select_prev();
    model.select_prev();  // clamps at 0
    BOOST_TEST(model.selected_index() == 0u);
}
