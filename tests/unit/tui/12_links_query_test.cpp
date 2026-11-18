#define BOOST_TEST_MODULE TuiLinksQueryTest

#include <string>

#include <boost/test/unit_test.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/links_query.hpp>
#include <url_shortener/tui/table_model.hpp>

#include "golden.hpp"
#include "hostile_strings.hpp"

/**
 * @file 12_links_query_test.cpp
 * @brief Filter-parse and request-bytes tests for the Links query model
 * (FTXUI-free model library, subtask 06.0/12).
 *
 * Type: unit (model library, links url_shortener_tui_model).
 * Scenario: the D3 filter bar parses into q / tag / campaign (including the
 *           empty `camp:` and unknown-prefix cases); `build_link_list_request`
 *           always sends `status` explicitly, `limit` is the page size,
 * `offset` is the page offset, a sort outside created_at|slug|updated_at is
 *           dropped, and the status all/active request bytes match their
 *           goldens; a hostile filter is JSON-escaped, never raw.
 * Boundaries: empty vs scoped vs mixed filter text; allowed vs rejected sort;
 *           page 1 vs page 4 offset.
 * On failure first check: parse_filter_text / build_link_list_request in
 *           src/tui/links_query.cpp, or the committed request goldens.
 */

using url_shortener::tui::build_link_list_request;
using url_shortener::tui::build_slug_request;
using url_shortener::tui::LinkListFilter;
using url_shortener::tui::PageQuery;
using url_shortener::tui::parse_filter_text;
using url_shortener::tui::status_to_wire;
using url_shortener::tui::StatusFilter;
using url_shortener::tui::testing::assert_matches_golden;
using url_shortener::tui::testing::hostile_samples;

namespace commands = url_shortener::commands;

// ---------------------------------------------------------------------------
// Filter parsing.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(parse_empty_filter)
{
    const LinkListFilter filter = parse_filter_text("", StatusFilter::all);
    BOOST_TEST(filter.q.empty());
    BOOST_TEST(filter.tag.empty());
    BOOST_TEST(filter.campaign.empty());
    BOOST_TEST((filter.status == StatusFilter::all));
}

BOOST_AUTO_TEST_CASE(parse_campaign_prefix)
{
    const LinkListFilter filter =
        parse_filter_text("camp:spring", StatusFilter::active);
    BOOST_TEST(filter.campaign == "spring");
    BOOST_TEST(filter.q.empty());
    BOOST_TEST(filter.tag.empty());
    BOOST_TEST((filter.status == StatusFilter::active));
}

BOOST_AUTO_TEST_CASE(parse_tag_prefix)
{
    const LinkListFilter filter =
        parse_filter_text("tag:docs", StatusFilter::all);
    BOOST_TEST(filter.tag == "docs");
    BOOST_TEST(filter.q.empty());
    BOOST_TEST(filter.campaign.empty());
}

BOOST_AUTO_TEST_CASE(parse_mixed_words)
{
    const LinkListFilter filter = parse_filter_text(
        "promo tag:docs spring camp:autumn launch", StatusFilter::all);
    BOOST_TEST(filter.tag == "docs");
    BOOST_TEST(filter.campaign == "autumn");
    BOOST_TEST(filter.q == "promo spring launch");
}

BOOST_AUTO_TEST_CASE(parse_campaign_empty_value)
{
    const LinkListFilter filter = parse_filter_text("camp:", StatusFilter::all);
    BOOST_TEST(filter.campaign.empty());
    BOOST_TEST(filter.q.empty());
}

BOOST_AUTO_TEST_CASE(parse_unknown_prefix_is_query)
{
    const LinkListFilter filter =
        parse_filter_text("color:red", StatusFilter::all);
    // An unrecognized `x:` prefix stays part of the free-text query.
    BOOST_TEST(filter.q == "color:red");
    BOOST_TEST(filter.tag.empty());
    BOOST_TEST(filter.campaign.empty());
}

BOOST_AUTO_TEST_CASE(parse_quotes_kept_verbatim)
{
    const LinkListFilter filter =
        parse_filter_text("\"hello world\"", StatusFilter::all);
    // Quotes are not special: they stay in the query, space-joined by token.
    BOOST_TEST(filter.q == "\"hello world\"");
}

// ---------------------------------------------------------------------------
// status_to_wire covers every radio choice.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(status_to_wire_values)
{
    BOOST_TEST(status_to_wire(StatusFilter::all) == "all");
    BOOST_TEST(status_to_wire(StatusFilter::active) == "active");
    BOOST_TEST(status_to_wire(StatusFilter::disabled) == "disabled");
    BOOST_TEST(status_to_wire(StatusFilter::deleted) == "deleted");
}

// ---------------------------------------------------------------------------
// Request bytes: status all / active default pages match their goldens.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(request_status_all_matches_golden)
{
    LinkListFilter filter;
    filter.status = StatusFilter::all;
    PageQuery page;  // offset 0, limit 20, no sort
    const commands::CommandRequest request =
        build_link_list_request(filter, page, "tui-1");
    BOOST_TEST(request.command == "link.list");
    BOOST_TEST(request.id == "tui-1");
    assert_matches_golden(request.args_json, "12_request_link_list_all.json");
}

BOOST_AUTO_TEST_CASE(request_status_active_matches_golden)
{
    LinkListFilter filter;
    filter.status = StatusFilter::active;
    PageQuery page;
    const commands::CommandRequest request =
        build_link_list_request(filter, page, "tui-2");
    assert_matches_golden(request.args_json,
                          "12_request_link_list_active.json");
}

// ---------------------------------------------------------------------------
// limit is the page size; offset is the page offset; status is always sent.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(limit_and_offset_track_page)
{
    LinkListFilter filter;
    PageQuery page;
    page.offset = 60;  // page 4 at 20/page
    page.limit = 20;
    const commands::CommandRequest request =
        build_link_list_request(filter, page, "tui-3");
    BOOST_TEST(request.args_json
               == "{\"status\":\"all\",\"offset\":60,\"limit\":20}");
}

// ---------------------------------------------------------------------------
// Scoped terms are included only when non-empty, in catalog order.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(scoped_terms_included_when_set)
{
    const LinkListFilter filter =
        parse_filter_text("promo tag:docs camp:autumn", StatusFilter::disabled);
    PageQuery page;
    const commands::CommandRequest request =
        build_link_list_request(filter, page, "tui-4");
    BOOST_TEST(
        request.args_json
        == "{\"status\":\"disabled\",\"q\":\"promo\",\"tag\":\"docs\","
           "\"campaign\":\"autumn\",\"offset\":0,\"limit\":20}");
}

// ---------------------------------------------------------------------------
// An allowed sort emits sort+order; a disallowed sort is dropped.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(allowed_sort_emitted)
{
    LinkListFilter filter;
    PageQuery page;
    page.sort_key = "created_at";
    page.descending = true;
    const commands::CommandRequest request =
        build_link_list_request(filter, page, "tui-5");
    BOOST_TEST(request.args_json
               == "{\"status\":\"all\",\"sort\":\"created_at\",\"order\":"
                  "\"desc\",\"offset\":0,\"limit\":20}");
}

BOOST_AUTO_TEST_CASE(disallowed_sort_dropped)
{
    LinkListFilter filter;
    PageQuery page;
    page.sort_key = "clicks";  // not in created_at|slug|updated_at
    const commands::CommandRequest request =
        build_link_list_request(filter, page, "tui-6");
    BOOST_TEST(request.args_json.find("sort") == std::string::npos);
    BOOST_TEST(request.args_json
               == "{\"status\":\"all\",\"offset\":0,\"limit\":20}");
}

BOOST_AUTO_TEST_CASE(slug_sort_ascending_order)
{
    LinkListFilter filter;
    PageQuery page;
    page.sort_key = "slug";
    page.descending = false;
    const commands::CommandRequest request =
        build_link_list_request(filter, page, "tui-7");
    BOOST_TEST(request.args_json.find("\"order\":\"asc\"")
               != std::string::npos);
}

// ---------------------------------------------------------------------------
// build_slug_request escapes the slug and names the command.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(slug_request_escapes_slug)
{
    const commands::CommandRequest request =
        build_slug_request("link.delete", "a\"b", "tui-8");
    BOOST_TEST(request.command == "link.delete");
    BOOST_TEST(request.args_json == "{\"slug\":\"a\\\"b\"}");
}

// ---------------------------------------------------------------------------
// C9: a hostile filter or slug is JSON-escaped, so the request bytes stay
// well-formed - every C0 control (<0x20), ESC and DEL is backslash-escaped and
// never appears raw. (C1 code points are valid in a JSON string and are
// neutralized at the render sink, not here; the screen test covers the grid.)
// On failure first check: build_* builds args only through jsonString.
// ---------------------------------------------------------------------------
namespace
{

/// Reports whether @p text carries a raw C0 control, ESC or DEL - the bytes
/// that would make the JSON envelope malformed.
bool has_raw_control(const std::string& text)
{
    for (unsigned char byte : text) {
        if ((byte < 0x20u && byte != 0x20u) || byte == 0x7Fu) {
            return true;
        }
    }
    return false;
}

}  // namespace

BOOST_AUTO_TEST_CASE(hostile_filter_is_escaped_not_raw)
{
    for (const auto& sample : hostile_samples()) {
        const LinkListFilter filter =
            parse_filter_text(sample.raw, StatusFilter::all);
        PageQuery page;
        const commands::CommandRequest list =
            build_link_list_request(filter, page, "tui-h");
        BOOST_TEST(
            !has_raw_control(list.args_json),
            "raw control byte in link.list args for sample: " << sample.name);

        const commands::CommandRequest slug =
            build_slug_request("link.delete", sample.raw, "tui-h");
        BOOST_TEST(!has_raw_control(slug.args_json),
                   "raw control byte in slug args for sample: " << sample.name);
    }
}
