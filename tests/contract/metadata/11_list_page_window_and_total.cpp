#define BOOST_TEST_MODULE MetadataContractListPageWindowTotalTest
#include <cstdio>
#include <string>

#include <boost/test/unit_test.hpp>

#include "metadata_contract_common.hpp"

namespace
{

/**
 * @brief Creates `count` links "c000".."cNNN" with distinct `created_at`
 * (one second apart) so the default created-ascending order equals the
 * insertion order on every backend, including the second-granularity SQL
 * stores.
 * @param harness Harness to seed.
 * @param count Number of links to create (expected < 1000 for the format).
 */
void SeedLinks(MetadataHarness& harness, int count)
{
    for (int i = 0; i < count; ++i) {
        char code[8];
        std::snprintf(code, sizeof(code), "c%03d", i);
        BOOST_REQUIRE(harness.repo->CreateLink(
            BasicRequest(code, "https://example.com"),
            std::string("id-") + code,
            harness.clock->now() + std::chrono::seconds(i)));
    }
}

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
 * @brief Joins a page's slugs into a comma-separated string.
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
 * [Contract][Metadata] Over seven rows, a `limit` of three returns three
 * windows (3/3/1 items) and `total` stays seven on every page, identically
 * on every backend.
 */
BOOST_FIXTURE_TEST_CASE(contract_list_page_pages_of_three_over_seven,
                        ResetSharedHarnesses)
{
    for (auto& h : SharedMetadataHarnesses()) {
        SeedLinks(h, 7);

        ListLinksQuery q;
        q.limit = 3;

        q.offset = 0;
        auto page0 = Page(h, q);
        BOOST_TEST(page0.total == std::size_t {7}, h.name << ": total page0");
        BOOST_TEST(Slugs(page0) == std::string("c000,c001,c002"),
                   h.name << ": page0 items");

        q.offset = 3;
        auto page1 = Page(h, q);
        BOOST_TEST(page1.total == std::size_t {7}, h.name << ": total page1");
        BOOST_TEST(Slugs(page1) == std::string("c003,c004,c005"),
                   h.name << ": page1 items");

        q.offset = 6;
        auto page2 = Page(h, q);
        BOOST_TEST(page2.total == std::size_t {7}, h.name << ": total page2");
        BOOST_TEST(Slugs(page2) == std::string("c006"),
                   h.name << ": page2 items");
    }
}

/**
 * [Contract][Metadata] An `offset` equal to `total` yields no items while
 * `total` still reports the full match count.
 */
BOOST_FIXTURE_TEST_CASE(contract_list_page_offset_equals_total,
                        ResetSharedHarnesses)
{
    for (auto& h : SharedMetadataHarnesses()) {
        SeedLinks(h, 7);

        ListLinksQuery q;
        q.offset = 7;
        q.limit = 3;
        auto page = Page(h, q);
        BOOST_TEST(page.total == std::size_t {7}, h.name << ": total");
        BOOST_TEST(page.items.empty(), h.name << ": items empty");
    }
}

/**
 * [Contract][Metadata] An `offset` beyond `total` yields no items while
 * `total` still reports the full match count.
 */
BOOST_FIXTURE_TEST_CASE(contract_list_page_offset_beyond_total,
                        ResetSharedHarnesses)
{
    for (auto& h : SharedMetadataHarnesses()) {
        SeedLinks(h, 7);

        ListLinksQuery q;
        q.offset = 100;
        q.limit = 3;
        auto page = Page(h, q);
        BOOST_TEST(page.total == std::size_t {7}, h.name << ": total");
        BOOST_TEST(page.items.empty(), h.name << ": items empty");
    }
}

/**
 * [Contract][Metadata] A `limit` of zero returns no items but still counts
 * every matching record in `total`.
 */
BOOST_FIXTURE_TEST_CASE(contract_list_page_limit_zero, ResetSharedHarnesses)
{
    for (auto& h : SharedMetadataHarnesses()) {
        SeedLinks(h, 7);

        ListLinksQuery q;
        q.offset = 0;
        q.limit = 0;
        auto page = Page(h, q);
        BOOST_TEST(page.total == std::size_t {7}, h.name << ": total");
        BOOST_TEST(page.items.empty(), h.name << ": items empty");
    }
}

/**
 * [Contract][Metadata] A single-row repository returns that one row with a
 * `total` of one.
 */
BOOST_FIXTURE_TEST_CASE(contract_list_page_single_row_repository,
                        ResetSharedHarnesses)
{
    for (auto& h : SharedMetadataHarnesses()) {
        SeedLinks(h, 1);

        ListLinksQuery q;
        auto page = Page(h, q);
        BOOST_TEST(page.total == std::size_t {1}, h.name << ": total");
        BOOST_TEST(Slugs(page) == std::string("c000"), h.name << ": items");
    }
}
