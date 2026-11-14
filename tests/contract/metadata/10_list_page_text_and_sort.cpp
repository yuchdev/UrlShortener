#define BOOST_TEST_MODULE MetadataContractListPageTextSortTest
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "metadata_contract_common.hpp"

namespace
{

/**
 * @brief Runs `ListLinksPage` for `query` on `harness` and requires success.
 * @param harness Backend harness to query.
 * @param query Filter/sort/page options.
 * @return The populated page; fails the test if the call returns `false`.
 */
LinkPage Page(MetadataHarness& harness, const ListLinksQuery& query)
{
    LinkPage page;
    RepoError error = RepoError::none;
    BOOST_REQUIRE_MESSAGE(harness.repo->ListLinksPage(query, &page, &error),
                          harness.name << ": ListLinksPage failed");
    return page;
}

/**
 * @brief Joins a page's slugs into a single comma-separated string so an
 * ordered expectation can be asserted as one readable value.
 * @param page Page whose items are read in order.
 * @return The `short_code`s of `page.items` joined with commas.
 */
std::string Slugs(const LinkPage& page)
{
    std::string out;
    for (const auto& record : page.items) {
        if (!out.empty()) {
            out += ",";
        }
        out += record.short_code;
    }
    return out;
}

}  // namespace

/**
 * [Contract][Metadata] A case-insensitive `text` filter keeps only the
 * records whose slug (or target URL) contains the needle, identically on
 * every backend. Slugs "abc" and "Abd" match "AB"; "xyz" does not.
 */
BOOST_FIXTURE_TEST_CASE(contract_list_page_text_case_insensitive_slug,
                        ResetSharedHarnesses)
{
    for (auto& h : SharedMetadataHarnesses()) {
        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("abc", "https://one.example"),
                               "id-abc",
                               h.clock->now()));
        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("Abd", "https://two.example"),
                               "id-Abd",
                               h.clock->now() + std::chrono::seconds(1)));
        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("xyz", "https://three.example"),
                               "id-xyz",
                               h.clock->now() + std::chrono::seconds(2)));

        ListLinksQuery q;
        q.text = "AB";
        auto page = Page(h, q);
        BOOST_TEST(page.total == std::size_t {2}, h.name << ": total");
        // Default sort is created_at ascending: abc (now) then Abd (now+1s).
        BOOST_TEST(Slugs(page) == std::string("abc,Abd"),
                   h.name << ": matched slugs");
    }
}

/**
 * [Contract][Metadata] The `text` filter also matches the target URL, not
 * only the slug: a record whose slug does not contain the needle is still
 * returned when its URL does.
 */
BOOST_FIXTURE_TEST_CASE(contract_list_page_text_matches_target_url_only,
                        ResetSharedHarnesses)
{
    for (auto& h : SharedMetadataHarnesses()) {
        BOOST_REQUIRE(h.repo->CreateLink(
            BasicRequest("zzz", "https://needle-in-url.example"),
            "id-zzz",
            h.clock->now()));
        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("yyy", "https://nothing.example"),
                               "id-yyy",
                               h.clock->now() + std::chrono::seconds(1)));

        ListLinksQuery q;
        q.text = "needle";
        auto page = Page(h, q);
        BOOST_TEST(page.total == std::size_t {1}, h.name << ": total");
        BOOST_TEST(Slugs(page) == std::string("zzz"), h.name << ": slugs");
    }
}

/**
 * [Contract][Metadata] A `text` filter that matches nothing yields an empty
 * page and a total of zero on every backend.
 */
BOOST_FIXTURE_TEST_CASE(contract_list_page_text_no_match_empty,
                        ResetSharedHarnesses)
{
    for (auto& h : SharedMetadataHarnesses()) {
        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("abc", "https://one.example"),
                               "id-abc",
                               h.clock->now()));

        ListLinksQuery q;
        q.text = "QQQ";
        auto page = Page(h, q);
        BOOST_TEST(page.total == std::size_t {0}, h.name << ": total");
        BOOST_TEST(page.items.empty(), h.name << ": items empty");
    }
}

/**
 * [Contract][Metadata] Sorting by `short_code` ascending orders by byte
 * value, so uppercase 'A' sorts before lowercase 'a': "Abd" < "abc" < "xyz".
 */
BOOST_FIXTURE_TEST_CASE(contract_list_page_sort_short_code_asc,
                        ResetSharedHarnesses)
{
    for (auto& h : SharedMetadataHarnesses()) {
        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("abc", "https://one.example"),
                               "id-abc",
                               h.clock->now()));
        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("Abd", "https://two.example"),
                               "id-Abd",
                               h.clock->now() + std::chrono::seconds(1)));
        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("xyz", "https://three.example"),
                               "id-xyz",
                               h.clock->now() + std::chrono::seconds(2)));

        ListLinksQuery q;
        q.sort_by = LinkSortField::short_code;
        q.order = SortOrder::asc;
        auto page = Page(h, q);
        BOOST_TEST(page.total == std::size_t {3}, h.name << ": total");
        BOOST_TEST(Slugs(page) == std::string("Abd,abc,xyz"),
                   h.name << ": asc order");
    }
}

/**
 * [Contract][Metadata] Sorting by `short_code` descending reverses the
 * byte-value order: "xyz" > "abc" > "Abd".
 */
BOOST_FIXTURE_TEST_CASE(contract_list_page_sort_short_code_desc,
                        ResetSharedHarnesses)
{
    for (auto& h : SharedMetadataHarnesses()) {
        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("abc", "https://one.example"),
                               "id-abc",
                               h.clock->now()));
        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("Abd", "https://two.example"),
                               "id-Abd",
                               h.clock->now() + std::chrono::seconds(1)));
        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("xyz", "https://three.example"),
                               "id-xyz",
                               h.clock->now() + std::chrono::seconds(2)));

        ListLinksQuery q;
        q.sort_by = LinkSortField::short_code;
        q.order = SortOrder::desc;
        auto page = Page(h, q);
        BOOST_TEST(page.total == std::size_t {3}, h.name << ": total");
        BOOST_TEST(Slugs(page) == std::string("xyz,abc,Abd"),
                   h.name << ": desc order");
    }
}

/**
 * [Contract][Metadata] Sorting by `created_at` descending returns the most
 * recently created record first. SQL stores timestamps as epoch seconds, so
 * records are seeded at least one second apart.
 */
BOOST_FIXTURE_TEST_CASE(contract_list_page_sort_created_at_desc,
                        ResetSharedHarnesses)
{
    for (auto& h : SharedMetadataHarnesses()) {
        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("aaa", "https://one.example"),
                               "id-aaa",
                               h.clock->now()));
        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("bbb", "https://two.example"),
                               "id-bbb",
                               h.clock->now() + std::chrono::seconds(1)));
        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("ccc", "https://three.example"),
                               "id-ccc",
                               h.clock->now() + std::chrono::seconds(2)));

        ListLinksQuery q;
        q.sort_by = LinkSortField::created_at;
        q.order = SortOrder::desc;
        auto page = Page(h, q);
        BOOST_TEST(page.total == std::size_t {3}, h.name << ": total");
        BOOST_TEST(Slugs(page) == std::string("ccc,bbb,aaa"),
                   h.name << ": created desc order");
    }
}

/**
 * [Contract][Metadata] When two records share an equal `created_at`, the
 * sort breaks the tie by `short_code` ascending, regardless of insertion
 * order, on every backend (SQL second-granularity ties included).
 */
BOOST_FIXTURE_TEST_CASE(contract_list_page_created_at_tie_breaks_by_short_code,
                        ResetSharedHarnesses)
{
    for (auto& h : SharedMetadataHarnesses()) {
        // Inserted qqq-before-ppp but both at the same instant: the tie-break
        // must still order ppp before qqq.
        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("qqq", "https://one.example"),
                               "id-qqq",
                               h.clock->now()));
        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("ppp", "https://two.example"),
                               "id-ppp",
                               h.clock->now()));

        ListLinksQuery q;
        q.sort_by = LinkSortField::created_at;
        q.order = SortOrder::asc;
        auto page = Page(h, q);
        BOOST_TEST(page.total == std::size_t {2}, h.name << ": total");
        BOOST_TEST(Slugs(page) == std::string("ppp,qqq"),
                   h.name << ": tie-break order");
    }
}
