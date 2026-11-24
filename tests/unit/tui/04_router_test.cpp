#define BOOST_TEST_MODULE TuiRouterTest

#include <set>
#include <utility>

#include <boost/test/unit_test.hpp>

#include <url_shortener/tui/router.hpp>
#include <url_shortener/tui/screen_id.hpp>

using url_shortener::tui::all_screen_ids;
using url_shortener::tui::Router;
using url_shortener::tui::ScreenId;

namespace
{

// A second, independent encoding of the D11 push-edge table, used to check
// Router::is_legal exhaustively. Tab transitions (digits / ':') are not push
// edges and are intentionally absent here.
//
// On failure first check: a D11 edge was added/removed in router.cpp without
// updating this table (or vice versa) - the two must agree edge-for-edge.
const std::set<std::pair<ScreenId, ScreenId>>& expected_edges()
{
    static const std::set<std::pair<ScreenId, ScreenId>> edges = {
        {ScreenId::dashboard, ScreenId::links},
        {ScreenId::dashboard, ScreenId::tables},
        {ScreenId::dashboard, ScreenId::import_export},
        {ScreenId::dashboard, ScreenId::jobs},
        {ScreenId::dashboard, ScreenId::palette},
        {ScreenId::links, ScreenId::link_detail},
        {ScreenId::links, ScreenId::link_form},
        {ScreenId::link_detail, ScreenId::link_form},
        {ScreenId::link_form, ScreenId::confirm},
        {ScreenId::confirm, ScreenId::links},
        {ScreenId::confirm, ScreenId::dashboard},
        {ScreenId::tables, ScreenId::row_json},
        {ScreenId::tables, ScreenId::link_form},
        {ScreenId::import_export, ScreenId::jobs},
        {ScreenId::palette, ScreenId::generic_form},
        {ScreenId::generic_form, ScreenId::confirm},
        {ScreenId::jobs, ScreenId::job_detail},
    };
    return edges;
}

}  // namespace

// ---------------------------------------------------------------------------
// A fresh router starts at the Dashboard tab root (D11 [*] -> Dashboard).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(starts_at_dashboard)
{
    Router router;
    BOOST_TEST((router.current() == ScreenId::dashboard));
    BOOST_TEST((router.active_tab() == ScreenId::dashboard));
    BOOST_TEST(router.stack().size() == 1u);
}

// ---------------------------------------------------------------------------
// is_legal matches the D11 edge table for all 14x14 ordered pairs.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(is_legal_matches_d11_edge_table_exhaustively)
{
    const auto& edges = expected_edges();
    for (ScreenId from : all_screen_ids())
    {
        for (ScreenId to : all_screen_ids())
        {
            const bool expected =
                edges.count(std::make_pair(from, to)) != 0;
            BOOST_TEST(Router::is_legal(from, to) == expected,
                       "edge " << static_cast<int>(from) << "->"
                               << static_cast<int>(to));
        }
    }
}

// ---------------------------------------------------------------------------
// push follows legal edges and leaves state untouched on an illegal edge.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(push_accepts_legal_rejects_illegal)
{
    Router router;
    BOOST_TEST(router.push(ScreenId::links));
    BOOST_TEST((router.current() == ScreenId::links));

    // links -> link_detail is illegal only as a second hop here: links ->
    // row_json is never legal.
    BOOST_TEST(!router.push(ScreenId::row_json));
    BOOST_TEST((router.current() == ScreenId::links));  // unchanged

    BOOST_TEST(router.push(ScreenId::link_detail));
    BOOST_TEST((router.current() == ScreenId::link_detail));
}

// ---------------------------------------------------------------------------
// pop at a tab root is a no-op; a nested screen pops back.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(pop_at_tab_root_is_a_noop)
{
    Router router;
    BOOST_TEST(!router.pop());
    BOOST_TEST((router.current() == ScreenId::dashboard));

    router.push(ScreenId::links);
    BOOST_TEST(router.pop());
    BOOST_TEST((router.current() == ScreenId::dashboard));
    BOOST_TEST(!router.pop());  // back at the root, no-op again
}

// ---------------------------------------------------------------------------
// A tab key from a nested screen resets the stack to that tab root.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(tab_key_from_nested_screen_resets_stack)
{
    Router router;
    router.navigate_tab('2');  // links
    router.push(ScreenId::link_detail);
    BOOST_TEST(router.stack().size() == 2u);

    BOOST_TEST(router.navigate_tab('3'));  // tables
    BOOST_TEST((router.current() == ScreenId::tables));
    BOOST_TEST(router.stack().size() == 1u);

    // A non-tab key is rejected and changes nothing.
    BOOST_TEST(!router.navigate_tab('x'));
    BOOST_TEST((router.current() == ScreenId::tables));
}

// ---------------------------------------------------------------------------
// All seven digit tabs plus ':' map to their screens.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(navigate_tab_covers_all_tabs)
{
    Router router;
    const std::pair<char, ScreenId> cases[] = {
        {'1', ScreenId::dashboard},     {'2', ScreenId::links},
        {'3', ScreenId::tables},        {'4', ScreenId::import_export},
        {'5', ScreenId::jobs},          {'6', ScreenId::analytics},
        {'7', ScreenId::settings},      {':', ScreenId::palette},
    };
    for (const auto& c : cases)
    {
        BOOST_TEST(router.navigate_tab(c.first));
        BOOST_TEST((router.current() == c.second));
        BOOST_TEST((router.active_tab() == c.second));
    }
}

// ---------------------------------------------------------------------------
// Confirm is modal: it highlights its return target and resolving it returns
// there, leaving no residue on the stack.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(confirm_return_target_restored_for_link_form)
{
    Router router;
    router.navigate_tab('2');  // links
    router.push(ScreenId::link_form);
    BOOST_TEST(router.push(ScreenId::confirm));

    // The modal highlights Links, not confirm.
    BOOST_TEST((router.current() == ScreenId::confirm));
    BOOST_TEST((router.active_tab() == ScreenId::links));

    // Resolving to the return target replaces the stack with that tab root.
    BOOST_TEST(router.push(ScreenId::links));
    BOOST_TEST((router.current() == ScreenId::links));
    BOOST_TEST(router.stack().size() == 1u);
}

// ---------------------------------------------------------------------------
// A palette/generic-form confirm returns to Dashboard (D11 Confirm --> Dashboard).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(confirm_return_target_restored_for_generic_form)
{
    Router router;
    router.navigate_tab(':');  // palette
    router.push(ScreenId::generic_form);
    BOOST_TEST(router.push(ScreenId::confirm));

    BOOST_TEST((router.active_tab() == ScreenId::dashboard));

    BOOST_TEST(router.push(ScreenId::dashboard));
    BOOST_TEST((router.current() == ScreenId::dashboard));
    BOOST_TEST(router.stack().size() == 1u);
}
