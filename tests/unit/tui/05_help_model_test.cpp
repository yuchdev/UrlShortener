#define BOOST_TEST_MODULE TuiHelpModelTest

#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/tui/help_model.hpp>
#include <url_shortener/tui/keymap.hpp>

/**
 * @file 05_help_model_test.cpp
 * @brief Unit tests for the F1 help-overlay content model.
 *
 * Type: unit (model library, FTXUI-free).
 * Scenario: global hints precede screen hints; exact duplicates are removed;
 *           an empty screen-hint list is handled.
 * Boundaries: a screen hint that exactly matches a global one (dropped) versus
 *             a screen hint that shares only the key (kept).
 * On failure first check: the dedup/order logic in `src/tui/help_model.cpp`.
 */

using url_shortener::tui::HelpEntry;
using url_shortener::tui::HelpModel;
using url_shortener::tui::KeyHint;

namespace
{

std::vector<KeyHint> global()
{
    return {KeyHint {"F1", "help"}, KeyHint {"q", "back"},
            KeyHint {"Ctrl-C", "quit"}};
}

}  // namespace

// ---------------------------------------------------------------------------
// Normal case: global entries come first, then the screen-specific ones.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(global_entries_precede_screen_entries)
{
    HelpModel model(global(), {KeyHint {"j/k", "move"},
                               KeyHint {"Enter", "open"}});
    const std::vector<HelpEntry> entries = model.entries();

    BOOST_REQUIRE_EQUAL(entries.size(), 5u);
    BOOST_TEST(entries[0].keys == "F1");
    BOOST_TEST(entries[1].keys == "q");
    BOOST_TEST(entries[2].keys == "Ctrl-C");
    BOOST_TEST(entries[3].keys == "j/k");
    BOOST_TEST(entries[3].label == "move");
    BOOST_TEST(entries[4].keys == "Enter");
}

// ---------------------------------------------------------------------------
// Exact duplicates are removed, keeping the first (global) occurrence.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(exact_duplicates_are_removed)
{
    HelpModel model(global(), {KeyHint {"q", "back"},  // duplicate of global
                               KeyHint {"r", "refresh"}});
    const std::vector<HelpEntry> entries = model.entries();

    BOOST_REQUIRE_EQUAL(entries.size(), 4u);
    // The duplicate "q back" appears once (from the global list) only.
    int q_back = 0;
    for (const HelpEntry& e : entries)
    {
        if (e.keys == "q" && e.label == "back")
        {
            ++q_back;
        }
    }
    BOOST_TEST(q_back == 1);
    BOOST_TEST(entries.back().keys == "r");
}

// ---------------------------------------------------------------------------
// Boundary: a screen hint sharing only the key (different label) is kept.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(same_key_different_label_is_kept)
{
    HelpModel model({KeyHint {"r", "restore"}}, {KeyHint {"r", "refresh"}});
    const std::vector<HelpEntry> entries = model.entries();

    BOOST_REQUIRE_EQUAL(entries.size(), 2u);
    BOOST_TEST(entries[0].label == "restore");
    BOOST_TEST(entries[1].label == "refresh");
}

// ---------------------------------------------------------------------------
// Edge case: an empty screen-hint list yields just the globals.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(empty_screen_hints_ok)
{
    HelpModel model(global(), {});
    const std::vector<HelpEntry> entries = model.entries();
    BOOST_REQUIRE_EQUAL(entries.size(), 3u);
    BOOST_TEST(entries[0].keys == "F1");

    HelpModel both_empty({}, {});
    BOOST_TEST(both_empty.entries().empty());
}
