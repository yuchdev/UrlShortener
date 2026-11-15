/**
 * @file 46_links_table_source.cpp
 * @brief Unit tests for @ref url_shortener::app::LinksTableSource: metadata,
 * exact per-row key set, `status=all` visibility, sort/`q` forwarding,
 * `row_count`, lister-error mapping, and hostile-input JSON validity.
 */
#define BOOST_TEST_MODULE AppLinksTableSourceTest
#include <cstddef>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/app/app_error.hpp"
#include "url_shortener/app/link_lister.hpp"
#include "url_shortener/app/links_table_source.hpp"
#include "url_shortener/core/types.hpp"

using url_shortener::LinkStatus;
using url_shortener::RedirectType;
using url_shortener::app::AppErrorCode;
using url_shortener::app::ILinkLister;
using url_shortener::app::LinkListPage;
using url_shortener::app::LinkListQuery;
using url_shortener::app::LinksTableSource;
using url_shortener::app::LinkView;
using url_shortener::app::list_sort;
using url_shortener::app::list_status;
using url_shortener::app::Result;
using url_shortener::app::TableColumn;
using url_shortener::app::TableQuery;
using url_shortener::app::TableRowsPage;

namespace
{

// A fake ILinkLister that records the last query it received and returns a
// configured page (or a storage_failure when m_fail is set).
class FakeLinkLister final : public ILinkLister
{
public:
    mutable LinkListQuery m_last_query;
    mutable bool m_called = false;
    LinkListPage m_page;
    bool m_fail = false;

    Result<LinkListPage> list(const LinkListQuery& query) const override
    {
        m_last_query = query;
        m_called = true;
        Result<LinkListPage> result;
        if (m_fail) {
            result.error.code = AppErrorCode::storage_failure;
            result.error.detail = "backend down";
            return result;
        }
        result.value = m_page;
        return result;
    }
};

// Builds a minimal LinkView with the fields the links source projects.
LinkView make_view(const std::string& id,
                   const std::string& slug,
                   const std::string& url,
                   LinkStatus status,
                   bool enabled)
{
    LinkView view;
    view.id = id;
    view.slug = slug;
    view.target_url = url;
    view.created_at = "2024-01-01T00:00:00Z";
    view.updated_at = "2024-01-02T00:00:00Z";
    view.enabled = enabled;
    view.redirect_type = RedirectType::temporary;
    view.status = status;
    return view;
}

// Extracts the ordered list of top-level object keys from a flat JSON object.
// A key is a string token at brace depth 1 immediately followed by ':'; string
// values (followed by ',' or '}') are skipped, so this never mistakes a value
// for a key.
std::vector<std::string> top_level_keys(const std::string& json)
{
    std::vector<std::string> keys;
    int depth = 0;
    std::size_t i = 0;
    const std::size_t n = json.size();
    while (i < n) {
        const char c = json[i];
        if (c == '{' || c == '[') {
            ++depth;
            ++i;
            continue;
        }
        if (c == '}' || c == ']') {
            --depth;
            ++i;
            continue;
        }
        if (c == '"') {
            std::string token;
            ++i;
            while (i < n) {
                const char d = json[i];
                if (d == '\\' && i + 1 < n) {
                    token.push_back(json[i + 1]);
                    i += 2;
                    continue;
                }
                if (d == '"') {
                    ++i;
                    break;
                }
                token.push_back(d);
                ++i;
            }
            if (depth == 1 && i < n && json[i] == ':') {
                keys.push_back(token);
            }
            continue;
        }
        ++i;
    }
    return keys;
}

const std::vector<std::string> k_expected_columns = {"id",
                                                     "slug",
                                                     "url",
                                                     "status",
                                                     "enabled",
                                                     "redirect_type",
                                                     "created_at",
                                                     "updated_at",
                                                     "expires_at",
                                                     "deleted_at"};

}  // namespace

// Type: unit (metadata: name, writable, exact column list and order).
// Scenario: a fresh source exposes name "links", writable true, and the fixed
//   ten-column list with the documented sortable/searchable flags.
// Boundaries: only slug/created_at/updated_at are sortable; only slug/url are
//   searchable.
// On failure first check: did the literal column list drift (contract C6)?
BOOST_AUTO_TEST_CASE(metadata_name_writable_and_columns)
{
    FakeLinkLister lister;
    LinksTableSource source(lister);

    BOOST_TEST(source.name() == "links");
    BOOST_TEST(source.writable());

    const std::vector<TableColumn>& columns = source.columns();
    BOOST_REQUIRE(columns.size() == k_expected_columns.size());
    for (std::size_t i = 0; i < columns.size(); ++i) {
        BOOST_TEST(columns[i].name == k_expected_columns[i]);
    }

    BOOST_TEST(columns[0].type == "string");  // id
    BOOST_TEST(columns[1].sortable);  // slug
    BOOST_TEST(columns[1].searchable);  // slug
    BOOST_TEST(columns[2].searchable);  // url
    BOOST_TEST(!columns[2].sortable);  // url
    BOOST_TEST(columns[4].type == "bool");  // enabled
    BOOST_TEST(columns[6].sortable);  // created_at
    BOOST_TEST(columns[6].type == "timestamp");  // created_at
    BOOST_TEST(columns[7].sortable);  // updated_at
    BOOST_TEST(!columns[8].sortable);  // expires_at
}

// Type: unit (per-row key set equals the column list).
// Scenario: one row is serialized and its top-level keys, in order, equal the
//   fixed column names - no extra keys (tags/metadata/campaign/short_url).
// Boundaries: the key set is exactly the ten columns.
// On failure first check: did serialize_link_row add or drop a field?
BOOST_AUTO_TEST_CASE(row_key_set_equals_columns)
{
    FakeLinkLister lister;
    lister.m_page.items = {make_view(
        "id-1", "alpha", "https://a.example.com", LinkStatus::active, true)};
    lister.m_page.total = 1;
    LinksTableSource source(lister);

    const Result<TableRowsPage> page = source.rows(TableQuery {});
    BOOST_REQUIRE(page.ok());
    BOOST_REQUIRE(page.value->rows_json.size() == 1u);

    const std::vector<std::string> keys =
        top_level_keys(page.value->rows_json[0]);
    BOOST_REQUIRE(keys.size() == k_expected_columns.size());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        BOOST_TEST(keys[i] == k_expected_columns[i]);
    }
}

// Type: unit (status=all visibility; absent timestamps are null).
// Scenario: a deleted, a disabled and an expired link are returned and their
//   `status` cells read "deleted"/"disabled"/"expired"; a link without
//   expires_at/deleted_at emits JSON null for those cells.
// Boundaries: the source always queries status=all, so every lifecycle state is
//   visible.
// On failure first check: did the source stop forcing status=all, or mis-map
//   the optional timestamps?
BOOST_AUTO_TEST_CASE(status_all_and_null_optionals)
{
    FakeLinkLister lister;
    LinkView deleted = make_view(
        "id-d", "del", "https://d.example.com", LinkStatus::deleted, true);
    deleted.deleted_at = "2024-02-01T00:00:00Z";
    LinkView disabled = make_view(
        "id-x", "dis", "https://x.example.com", LinkStatus::disabled, false);
    LinkView expired = make_view(
        "id-e", "exp", "https://e.example.com", LinkStatus::expired, true);
    expired.expires_at = "2020-01-01T00:00:00Z";

    lister.m_page.items = {deleted, disabled, expired};
    lister.m_page.total = 3;
    LinksTableSource source(lister);

    const Result<TableRowsPage> page = source.rows(TableQuery {});
    BOOST_REQUIRE(page.ok());
    BOOST_REQUIRE(page.value->rows_json.size() == 3u);

    // The source must have asked for status=all.
    BOOST_TEST((lister.m_last_query.status == list_status::all));

    BOOST_TEST(page.value->rows_json[0].find("\"status\":\"deleted\"")
               != std::string::npos);
    BOOST_TEST(
        page.value->rows_json[0].find("\"deleted_at\":\"2024-02-01T00:00:00Z\"")
        != std::string::npos);
    BOOST_TEST(page.value->rows_json[1].find("\"status\":\"disabled\"")
               != std::string::npos);
    BOOST_TEST(page.value->rows_json[1].find("\"enabled\":false")
               != std::string::npos);
    // disabled link has no expires_at/deleted_at -> null.
    BOOST_TEST(page.value->rows_json[1].find("\"expires_at\":null")
               != std::string::npos);
    BOOST_TEST(page.value->rows_json[1].find("\"deleted_at\":null")
               != std::string::npos);
    BOOST_TEST(page.value->rows_json[2].find("\"status\":\"expired\"")
               != std::string::npos);
    BOOST_TEST(
        page.value->rows_json[2].find("\"expires_at\":\"2020-01-01T00:00:00Z\"")
        != std::string::npos);
}

// Type: unit (sort and q forwarded to the lister).
// Scenario: a query with sort="updated_at", descending and q="abc" forwards
//   list_sort::updated_at, descending=true and q="abc" to the lister.
// Boundaries: an absent sort leaves the lister at its natural order; a present,
//   whitelisted sort is mapped.
// On failure first check: did to_list_sort mis-map, or did q stop forwarding?
BOOST_AUTO_TEST_CASE(sort_and_q_forwarded)
{
    FakeLinkLister lister;
    lister.m_page.total = 0;
    LinksTableSource source(lister);

    TableQuery query;
    query.sort = "updated_at";
    query.descending = true;
    query.q = "abc";
    query.offset = 7;
    query.limit = 25;

    const Result<TableRowsPage> page = source.rows(query);
    BOOST_REQUIRE(page.ok());

    BOOST_TEST((lister.m_last_query.status == list_status::all));
    BOOST_TEST((lister.m_last_query.sort == list_sort::updated_at));
    BOOST_TEST(lister.m_last_query.descending);
    BOOST_REQUIRE(lister.m_last_query.q.has_value());
    BOOST_TEST(*lister.m_last_query.q == "abc");
    BOOST_TEST(lister.m_last_query.offset == 7u);
    BOOST_TEST(lister.m_last_query.limit == 25u);
}

// Type: unit (row_count delegates to the lister total).
// Scenario: the lister reports total=42 and the source's row_count returns 42,
//   having asked with status=all and limit=1 (no window materialized).
// Boundaries: row_count mirrors the filtered total, not the page size.
// On failure first check: did row_count read items.size() instead of total?
BOOST_AUTO_TEST_CASE(row_count_equals_total)
{
    FakeLinkLister lister;
    lister.m_page.total = 42;
    LinksTableSource source(lister);

    const Result<std::size_t> count = source.row_count();
    BOOST_REQUIRE(count.ok());
    BOOST_TEST(*count.value == 42u);
    BOOST_TEST((lister.m_last_query.status == list_status::all));
    BOOST_TEST(lister.m_last_query.limit == 1u);
}

// Type: unit (lister failure maps to storage_failure).
// Scenario: a failing lister makes both rows() and row_count() return
//   storage_failure.
// Boundaries: the source never surfaces the lister's own detail text verbatim
//   as a value; it uses a fixed detail.
// On failure first check: did the source forward the error instead of mapping?
BOOST_AUTO_TEST_CASE(lister_error_maps_to_storage_failure)
{
    FakeLinkLister lister;
    lister.m_fail = true;
    LinksTableSource source(lister);

    const Result<TableRowsPage> page = source.rows(TableQuery {});
    BOOST_TEST(!page.ok());
    BOOST_TEST((page.error.code == AppErrorCode::storage_failure));

    const Result<std::size_t> count = source.row_count();
    BOOST_TEST(!count.ok());
    BOOST_TEST((count.error.code == AppErrorCode::storage_failure));
}

// Type: unit (hostile slug/URL stays valid JSON, T-51/T-52).
// Scenario: a link whose slug and URL carry quotes, a backslash, a newline and
//   a control byte is serialized; the raw control byte and bare quote do not
//   survive and the value is escaped.
// Boundaries: every cell is routed through jsonString, so no raw quote,
//   backslash, newline or control byte reaches the row.
// On failure first check: did a cell bypass jsonString?
BOOST_AUTO_TEST_CASE(hostile_slug_and_url_stay_valid_json)
{
    FakeLinkLister lister;
    LinkView view = make_view(
        "id-h", "a\"b\\c", "https://x/\n\x01?q=1", LinkStatus::active, true);
    lister.m_page.items = {view};
    lister.m_page.total = 1;
    LinksTableSource source(lister);

    const Result<TableRowsPage> page = source.rows(TableQuery {});
    BOOST_REQUIRE(page.ok());
    const std::string& row = page.value->rows_json[0];

    // No raw control byte or newline survives.
    BOOST_TEST(row.find('\n') == std::string::npos);
    BOOST_TEST(row.find('\x01') == std::string::npos);
    // Escaped forms are present.
    BOOST_TEST(row.find("a\\\"b\\\\c") != std::string::npos);
    BOOST_TEST(row.find("\\u0001") != std::string::npos);
    // The structural keys are intact (object parses to the full key set).
    BOOST_TEST(top_level_keys(row).size() == k_expected_columns.size());
}
