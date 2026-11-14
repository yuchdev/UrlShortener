#define BOOST_TEST_MODULE ListQueryEvalTest
#include <chrono>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/storage/list_links_eval.hpp"
#include "url_shortener/storage/models/link_record.hpp"
#include "url_shortener/storage/models/list_links_query.hpp"

namespace
{

using Clock = std::chrono::system_clock;

LinkRecord MakeRecord(const std::string& id,
                      const std::string& short_code,
                      const std::string& target_url,
                      int created_offset,
                      int updated_offset)
{
    LinkRecord r;
    r.id = id;
    r.short_code = short_code;
    r.target_url = target_url;
    r.created_at = Clock::time_point {} + std::chrono::seconds(created_offset);
    r.updated_at = Clock::time_point {} + std::chrono::seconds(updated_offset);
    r.is_active = true;
    return r;
}

std::vector<std::string> Codes(const LinkPage& page)
{
    std::vector<std::string> out;
    for (const auto& r : page.items) {
        out.push_back(r.short_code);
    }
    return out;
}

std::vector<LinkRecord> SampleRecords()
{
    // Created order: aaa(0) < bbb(1) < ccc(2); updated reversed.
    std::vector<LinkRecord> v;
    v.push_back(MakeRecord("1", "aaa", "https://example.com/alpha", 0, 2));
    v.push_back(MakeRecord("2", "bbb", "https://example.com/BETA", 1, 1));
    v.push_back(MakeRecord("3", "ccc", "https://other.test/gamma", 2, 0));
    return v;
}

}  // namespace

// Case 1: default query reproduces legacy order (created asc) and legacy
// include flags; total counts every record.
BOOST_AUTO_TEST_CASE(default_query_equals_legacy_order)
{
    ListLinksQuery q;
    LinkPage page = ApplyListQuery(SampleRecords(), q);
    BOOST_TEST(page.total == 3u);
    std::vector<std::string> expected {"aaa", "bbb", "ccc"};
    BOOST_TEST(Codes(page) == expected, boost::test_tools::per_element());
}

// text matches slug or URL, case-insensitive; no match -> empty, total 0.
BOOST_AUTO_TEST_CASE(text_filter_matches_slug_or_url_case_insensitive)
{
    ListLinksQuery q;
    q.text = std::string("beta");  // matches target_url of bbb (BETA)
    LinkPage page = ApplyListQuery(SampleRecords(), q);
    BOOST_TEST(page.total == 1u);
    std::vector<std::string> expected {"bbb"};
    BOOST_TEST(Codes(page) == expected, boost::test_tools::per_element());

    ListLinksQuery q_slug;
    q_slug.text = std::string("CC");  // matches short_code ccc
    LinkPage page_slug = ApplyListQuery(SampleRecords(), q_slug);
    BOOST_TEST(page_slug.total == 1u);

    ListLinksQuery q_none;
    q_none.text = std::string("no-such-text");
    LinkPage page_none = ApplyListQuery(SampleRecords(), q_none);
    BOOST_TEST(page_none.total == 0u);
    BOOST_TEST(page_none.items.empty());
}

// Sort by each field, both orders, with short_code tie-break.
BOOST_AUTO_TEST_CASE(sort_by_fields_and_orders)
{
    {
        ListLinksQuery q;
        q.sort_by = LinkSortField::created_at;
        q.order = SortOrder::desc;
        std::vector<std::string> expected {"ccc", "bbb", "aaa"};
        BOOST_TEST(Codes(ApplyListQuery(SampleRecords(), q)) == expected,
                   boost::test_tools::per_element());
    }
    {
        ListLinksQuery q;
        q.sort_by = LinkSortField::updated_at;
        q.order = SortOrder::asc;
        std::vector<std::string> expected {"ccc", "bbb", "aaa"};
        BOOST_TEST(Codes(ApplyListQuery(SampleRecords(), q)) == expected,
                   boost::test_tools::per_element());
    }
    {
        ListLinksQuery q;
        q.sort_by = LinkSortField::short_code;
        q.order = SortOrder::desc;
        std::vector<std::string> expected {"ccc", "bbb", "aaa"};
        BOOST_TEST(Codes(ApplyListQuery(SampleRecords(), q)) == expected,
                   boost::test_tools::per_element());
    }
}

// Tie-break on short_code ascending when the sort field is equal.
BOOST_AUTO_TEST_CASE(tie_break_on_short_code_ascending)
{
    std::vector<LinkRecord> v;
    // All share created_at == 5; short_code inserted out of order.
    v.push_back(MakeRecord("1", "mmm", "https://m", 5, 0));
    v.push_back(MakeRecord("2", "aaa", "https://a", 5, 0));
    v.push_back(MakeRecord("3", "zzz", "https://z", 5, 0));
    ListLinksQuery q;
    q.sort_by = LinkSortField::created_at;
    q.order = SortOrder::desc;  // tie-break stays short_code ascending
    std::vector<std::string> expected {"aaa", "mmm", "zzz"};
    BOOST_TEST(Codes(ApplyListQuery(std::move(v), q)) == expected,
               boost::test_tools::per_element());
}

// offset == total and offset > total -> empty items, correct total.
BOOST_AUTO_TEST_CASE(offset_at_or_beyond_total)
{
    {
        ListLinksQuery q;
        q.offset = 3;
        LinkPage page = ApplyListQuery(SampleRecords(), q);
        BOOST_TEST(page.total == 3u);
        BOOST_TEST(page.items.empty());
    }
    {
        ListLinksQuery q;
        q.offset = 10;
        LinkPage page = ApplyListQuery(SampleRecords(), q);
        BOOST_TEST(page.total == 3u);
        BOOST_TEST(page.items.empty());
    }
}

// limit == 0 -> empty items, correct total.
BOOST_AUTO_TEST_CASE(limit_zero_empty_items_total_correct)
{
    ListLinksQuery q;
    q.limit = 0;
    LinkPage page = ApplyListQuery(SampleRecords(), q);
    BOOST_TEST(page.total == 3u);
    BOOST_TEST(page.items.empty());
}

// A mid-range window reports the whole total but a bounded item window.
BOOST_AUTO_TEST_CASE(window_reports_total_not_window_size)
{
    ListLinksQuery q;
    q.offset = 1;
    q.limit = 1;
    LinkPage page = ApplyListQuery(SampleRecords(), q);
    BOOST_TEST(page.total == 3u);
    std::vector<std::string> expected {"bbb"};
    BOOST_TEST(Codes(page) == expected, boost::test_tools::per_element());
}

// extra_filter applied before counting and before windowing.
BOOST_AUTO_TEST_CASE(extra_filter_applied_before_count_and_window)
{
    ListLinksQuery q;
    q.extra_filter = [](const LinkRecord& r) { return r.short_code != "bbb"; };
    LinkPage page = ApplyListQuery(SampleRecords(), q);
    BOOST_TEST(page.total == 2u);
    std::vector<std::string> expected {"aaa", "ccc"};
    BOOST_TEST(Codes(page) == expected, boost::test_tools::per_element());
}

// extra_filter returning false for all -> empty.
BOOST_AUTO_TEST_CASE(extra_filter_rejecting_all)
{
    ListLinksQuery q;
    q.extra_filter = [](const LinkRecord&) { return false; };
    LinkPage page = ApplyListQuery(SampleRecords(), q);
    BOOST_TEST(page.total == 0u);
    BOOST_TEST(page.items.empty());
}

// owner filter keeps only matching owners.
BOOST_AUTO_TEST_CASE(owner_filter)
{
    std::vector<LinkRecord> v = SampleRecords();
    v[0].owner = "alice";
    v[1].owner = "bob";
    v[2].owner = "alice";
    ListLinksQuery q;
    q.owner = std::string("alice");
    LinkPage page = ApplyListQuery(std::move(v), q);
    BOOST_TEST(page.total == 2u);
    std::vector<std::string> expected {"aaa", "ccc"};
    BOOST_TEST(Codes(page) == expected, boost::test_tools::per_element());
}

// include_inactive=false drops inactive records.
BOOST_AUTO_TEST_CASE(include_inactive_flag)
{
    std::vector<LinkRecord> v = SampleRecords();
    v[1].is_active = false;
    ListLinksQuery q;
    q.include_inactive = false;
    LinkPage page = ApplyListQuery(std::move(v), q);
    BOOST_TEST(page.total == 2u);
    std::vector<std::string> expected {"aaa", "ccc"};
    BOOST_TEST(Codes(page) == expected, boost::test_tools::per_element());
}

// include_deleted=false drops records with deleted_at (default).
BOOST_AUTO_TEST_CASE(include_deleted_flag_drops_deleted)
{
    std::vector<LinkRecord> v = SampleRecords();
    v[2].deleted_at = Clock::time_point {} + std::chrono::seconds(99);
    ListLinksQuery q;  // include_deleted defaults to false
    LinkPage page = ApplyListQuery(std::move(v), q);
    BOOST_TEST(page.total == 2u);
    std::vector<std::string> expected {"aaa", "bbb"};
    BOOST_TEST(Codes(page) == expected, boost::test_tools::per_element());

    ListLinksQuery q_incl;
    q_incl.include_deleted = true;
    std::vector<LinkRecord> v2 = SampleRecords();
    v2[2].deleted_at = Clock::time_point {} + std::chrono::seconds(99);
    LinkPage page_incl = ApplyListQuery(std::move(v2), q_incl);
    BOOST_TEST(page_incl.total == 3u);
}

// LinkRecordMatches reflects the same native-filter semantics in isolation.
BOOST_AUTO_TEST_CASE(link_record_matches_predicate)
{
    LinkRecord r = MakeRecord("1", "abc", "https://host/path", 0, 0);
    ListLinksQuery q;
    BOOST_TEST(LinkRecordMatches(r, q));

    q.text = std::string("PATH");
    BOOST_TEST(LinkRecordMatches(r, q));
    q.text = std::string("missing");
    BOOST_TEST(!LinkRecordMatches(r, q));
}
