#define BOOST_TEST_MODULE MetadataContractListPageFlagsPredicateTest
#include <string>

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
 * [Contract][Metadata] `include_inactive = false` drops disabled records;
 * `include_inactive = true` keeps them, identically on every backend.
 */
BOOST_FIXTURE_TEST_CASE(contract_list_page_include_inactive_flag,
                        ResetSharedHarnesses)
{
    for (auto& h : SharedMetadataHarnesses()) {
        auto active = BasicRequest("act", "https://one.example");
        auto inactive = BasicRequest("ina", "https://two.example");
        inactive.is_active = false;
        BOOST_REQUIRE(h.repo->CreateLink(active, "id-act", h.clock->now()));
        BOOST_REQUIRE(h.repo->CreateLink(
            inactive, "id-ina", h.clock->now() + std::chrono::seconds(1)));

        ListLinksQuery q;
        q.sort_by = LinkSortField::short_code;
        q.order = SortOrder::asc;

        q.include_inactive = false;
        auto excluded = Page(h, q);
        BOOST_TEST(excluded.total == std::size_t {1},
                   h.name << ": total without inactive");
        BOOST_TEST(Slugs(excluded) == std::string("act"),
                   h.name << ": active only");

        q.include_inactive = true;
        auto included = Page(h, q);
        BOOST_TEST(included.total == std::size_t {2},
                   h.name << ": total with inactive");
        BOOST_TEST(Slugs(included) == std::string("act,ina"),
                   h.name << ": both links");
    }
}

/**
 * [Contract][Metadata] The `owner` filter narrows to exactly that owner's
 * rows and counts only them in `total`. This case runs on the Postgres
 * harness too (threat model T-50): it fails against the pre-fix
 * `(:owner_id IS NULL OR owner_id = :owner_id)` bind that returned zero
 * rows, so it guards subtask 01's `(:has_owner = 0 OR owner_id = :owner_id)`
 * fix. The per-case `ResetMetadataHarness` fixture empties the shared
 * Postgres `links` table first, so a shared database cannot accumulate rows
 * across cases or runs.
 */
BOOST_FIXTURE_TEST_CASE(contract_list_page_owner_narrows, ResetSharedHarnesses)
{
    for (auto& h : SharedMetadataHarnesses()) {
        auto o1a = BasicRequest("o1a", "https://one.example");
        o1a.owner = "owner-one";
        auto o1b = BasicRequest("o1b", "https://two.example");
        o1b.owner = "owner-one";
        auto o2a = BasicRequest("o2a", "https://three.example");
        o2a.owner = "owner-two";
        BOOST_REQUIRE(h.repo->CreateLink(o1a, "id-o1a", h.clock->now()));
        BOOST_REQUIRE(h.repo->CreateLink(
            o1b, "id-o1b", h.clock->now() + std::chrono::seconds(1)));
        BOOST_REQUIRE(h.repo->CreateLink(
            o2a, "id-o2a", h.clock->now() + std::chrono::seconds(2)));

        ListLinksQuery q;
        q.owner = "owner-one";
        q.sort_by = LinkSortField::short_code;
        q.order = SortOrder::asc;
        auto page = Page(h, q);
        BOOST_TEST(page.total == std::size_t {2},
                   h.name << ": owner-one total");
        BOOST_TEST(Slugs(page) == std::string("o1a,o1b"),
                   h.name << ": owner-one rows only");
    }
}

/**
 * [Contract][Metadata] An `extra_filter` predicate runs as a final filter:
 * only records it accepts are returned, and `total` counts the matches after
 * the predicate. Here it keeps records whose `state` attribute is "active".
 */
BOOST_FIXTURE_TEST_CASE(contract_list_page_extra_filter_attribute,
                        ResetSharedHarnesses)
{
    for (auto& h : SharedMetadataHarnesses()) {
        auto active = BasicRequest("sa1", "https://one.example");
        active.attributes["state"] = "active";
        auto archived = BasicRequest("sa2", "https://two.example");
        archived.attributes["state"] = "archived";
        BOOST_REQUIRE(h.repo->CreateLink(active, "id-sa1", h.clock->now()));
        BOOST_REQUIRE(h.repo->CreateLink(
            archived, "id-sa2", h.clock->now() + std::chrono::seconds(1)));

        ListLinksQuery q;
        q.extra_filter = [](const LinkRecord& record)
        {
            auto it = record.attributes.find("state");
            return it != record.attributes.end() && it->second == "active";
        };
        auto page = Page(h, q);
        BOOST_TEST(page.total == std::size_t {1},
                   h.name << ": total after predicate");
        BOOST_TEST(Slugs(page) == std::string("sa1"),
                   h.name << ": only active state");
    }
}

/**
 * [Contract][Metadata] An `extra_filter` that rejects everything yields an
 * empty page and `total == 0` on every backend.
 */
BOOST_FIXTURE_TEST_CASE(contract_list_page_extra_filter_rejects_all,
                        ResetSharedHarnesses)
{
    for (auto& h : SharedMetadataHarnesses()) {
        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("rr1", "https://one.example"),
                               "id-rr1",
                               h.clock->now()));
        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("rr2", "https://two.example"),
                               "id-rr2",
                               h.clock->now() + std::chrono::seconds(1)));

        ListLinksQuery q;
        q.extra_filter = [](const LinkRecord&) { return false; };
        auto page = Page(h, q);
        BOOST_TEST(page.total == std::size_t {0}, h.name << ": total");
        BOOST_TEST(page.items.empty(), h.name << ": items empty");
    }
}

/**
 * [Contract][Metadata] `include_deleted` hides or shows soft-deleted
 * records. In-memory only: the SQL backends do not persist
 * `LinkRecord::deleted_at` (Task 02.0 stores deletion in the `lnk_deleted`
 * attribute and the lister filters it via the predicate), so running this on
 * SQL would assert a capability they do not have. This is a documented
 * capability gap, not a masked defect.
 */
BOOST_FIXTURE_TEST_CASE(contract_list_page_include_deleted_in_memory_only,
                        ResetSharedHarnesses)
{
    for (auto& h : SharedMetadataHarnesses()) {
        if (h.name != "inmemory") {
            BOOST_TEST_MESSAGE(h.name << ": skipped (SQL does not persist "
                                         "deleted_at; see Task 02.0)");
            continue;
        }

        BOOST_REQUIRE(
            h.repo->CreateLink(BasicRequest("del", "https://one.example"),
                               "id-del",
                               h.clock->now()));
        auto record = h.repo->GetByShortCode("del");
        BOOST_REQUIRE(record.has_value());
        record->deleted_at = h.clock->now() + std::chrono::seconds(1);
        BOOST_REQUIRE(h.repo->UpdateLink(*record));

        ListLinksQuery q;
        q.include_deleted = false;
        auto hidden = Page(h, q);
        BOOST_TEST(hidden.total == std::size_t {0},
                   h.name << ": soft-deleted hidden by default");
        BOOST_TEST(hidden.items.empty(), h.name << ": no items when hidden");

        q.include_deleted = true;
        auto shown = Page(h, q);
        BOOST_TEST(shown.total == std::size_t {1},
                   h.name << ": soft-deleted shown when requested");
        BOOST_TEST(Slugs(shown) == std::string("del"),
                   h.name << ": deleted record returned");
    }
}
