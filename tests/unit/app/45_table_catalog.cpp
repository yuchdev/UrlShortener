#define BOOST_TEST_MODULE AppTableCatalogTest
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/app/app_error.hpp"
#include "url_shortener/app/table_source.hpp"

using url_shortener::app::AppError;
using url_shortener::app::AppErrorCode;
using url_shortener::app::ITableSource;
using url_shortener::app::Result;
using url_shortener::app::serializeTableRowsJson;
using url_shortener::app::serializeTablesJson;
using url_shortener::app::TableCatalog;
using url_shortener::app::TableColumn;
using url_shortener::app::TableQuery;
using url_shortener::app::TableRowsPage;

namespace
{

// A minimal, in-test ITableSource whose name, writability, columns, row count
// and rows page are all configured per-test. The row-returning members are
// const on the port; the page/count are plain values returned by copy.
class FakeTableSource : public ITableSource
{
public:
    std::string m_name;
    bool m_writable = false;
    std::vector<TableColumn> m_columns;
    std::size_t m_count = 0;
    TableRowsPage m_page;

    const std::string& name() const override { return m_name; }
    bool writable() const override { return m_writable; }
    const std::vector<TableColumn>& columns() const override
    {
        return m_columns;
    }

    Result<std::size_t> row_count() const override
    {
        Result<std::size_t> result;
        result.value = m_count;
        return result;
    }

    Result<TableRowsPage> rows(const TableQuery& /*query*/) const override
    {
        Result<TableRowsPage> result;
        result.value = m_page;
        return result;
    }
};

// Builds a shared FakeTableSource with just a name (the common case for the
// add/find/forbidden tests, where columns and rows are irrelevant).
std::shared_ptr<FakeTableSource> named(const std::string& name)
{
    auto source = std::make_shared<FakeTableSource>();
    source->m_name = name;
    return source;
}

TableColumn column(const std::string& name,
                   const std::string& type,
                   bool sortable,
                   bool searchable)
{
    TableColumn col;
    col.name = name;
    col.type = type;
    col.sortable = sortable;
    col.searchable = searchable;
    return col;
}

}  // namespace

// Type: unit (registration, lookup and order; contract C6 allowlist).
// Scenario: two sources added in order are found by exact name and returned by
//   all() in registration order; an unregistered name is not found.
// Boundaries: find() matches byte-for-byte; all() preserves insertion order.
// On failure first check: did add() drop a source, or did find()/all() reorder?
BOOST_AUTO_TEST_CASE(add_find_and_all_preserve_order)
{
    TableCatalog catalog;

    BOOST_TEST(catalog.add(named("links")));
    BOOST_TEST(catalog.add(named("click_events")));

    BOOST_TEST(catalog.all().size() == 2u);
    BOOST_TEST(catalog.all()[0]->name() == "links");
    BOOST_TEST(catalog.all()[1]->name() == "click_events");

    BOOST_REQUIRE(catalog.find("links") != nullptr);
    BOOST_TEST(catalog.find("links")->name() == "links");
    BOOST_REQUIRE(catalog.find("click_events") != nullptr);
    BOOST_TEST(catalog.find("unregistered") == nullptr);
}

// Type: unit (duplicate rejected).
// Scenario: adding a second source with an already-registered name returns
// false
//   and leaves the catalog unchanged.
// Boundaries: the first add wins; the duplicate never displaces it.
// On failure first check: did add() stop checking find() before inserting?
BOOST_AUTO_TEST_CASE(duplicate_name_rejected)
{
    TableCatalog catalog;

    BOOST_TEST(catalog.add(named("links")));
    BOOST_TEST(!catalog.add(named("links")));
    BOOST_TEST(catalog.all().size() == 1u);
}

// Type: unit (malformed names rejected, threat model T-51).
// Scenario: names violating [a-z][a-z0-9_]* (uppercase, leading digit, empty,
//   dash, dot, space) are each rejected and nothing is registered.
// Boundaries: the charset/length rule is the only gate besides the forbidden
//   set and duplication.
// On failure first check: did is_valid_table_name admit one of these shapes?
BOOST_AUTO_TEST_CASE(malformed_names_rejected)
{
    TableCatalog catalog;

    BOOST_TEST(!catalog.add(named("Links")));  // uppercase
    BOOST_TEST(!catalog.add(named("1x")));  // leading digit
    BOOST_TEST(!catalog.add(named("")));  // empty
    BOOST_TEST(!catalog.add(named("a-b")));  // dash
    BOOST_TEST(!catalog.add(named("a.b")));  // dot
    BOOST_TEST(!catalog.add(named("a b")));  // space
    BOOST_TEST(catalog.all().empty());
}

// Type: unit (null source rejected).
// Scenario: adding a null shared_ptr returns false and registers nothing.
// Boundaries: a null source never dereferences name().
// On failure first check: did add() dereference source before the null guard?
BOOST_AUTO_TEST_CASE(null_source_rejected)
{
    TableCatalog catalog;

    BOOST_TEST(!catalog.add(std::shared_ptr<ITableSource>()));
    BOOST_TEST(catalog.all().empty());
}

// Type: unit (forbidden auth tables rejected, success criterion 3, T-51).
// Scenario: each of the five forbidden auth-table names is refused by add().
// Boundaries: these names pass the charset rule, so the forbidden-set guard is
//   what rejects them; the set must contain exactly these five.
// On failure first check: did a name drop out of k_forbidden_names?
BOOST_AUTO_TEST_CASE(forbidden_auth_table_names_rejected)
{
    const std::vector<std::string> forbidden = {
        "app_users",
        "auth_sessions",
        "service_clients",
        "auth_audit_log",
        "user_credentials",
    };

    for (const std::string& name : forbidden) {
        TableCatalog catalog;
        BOOST_TEST(!catalog.add(named(name)),
                   "forbidden name was admitted: " << name);
        BOOST_TEST(catalog.all().empty());
    }
}

// Type: unit (forbidden set matched case-insensitively, T-51).
// Scenario: a mixed-case forbidden name is still refused.
// Boundaries: the charset rule would reject uppercase anyway, but the forbidden
//   guard runs first and must fold case so the refusal is categorical.
// On failure first check: did is_forbidden_name stop lowercasing?
BOOST_AUTO_TEST_CASE(forbidden_names_rejected_case_insensitively)
{
    TableCatalog catalog;

    BOOST_TEST(!catalog.add(named("App_Users")));
    BOOST_TEST(!catalog.add(named("AUTH_SESSIONS")));
    BOOST_TEST(catalog.all().empty());
}

// Type: unit (db.tables serialization shape, empty catalog).
// Scenario: an empty catalog serializes to {"tables":[]}.
// Boundaries: the array is present and empty, not omitted.
// On failure first check: did serializeTablesJson change the empty form?
BOOST_AUTO_TEST_CASE(serialize_tables_empty)
{
    TableCatalog catalog;

    BOOST_TEST(serializeTablesJson(catalog, {}) == R"({"tables":[]})");
}

// Type: unit (db.tables serialization shape, two tables).
// Scenario: two sources with columns and parallel row counts serialize to the
//   full
//   {"tables":[{name,rows,writable,columns:[{name,type,sortable,searchable}]}]}
//   shape in registration order.
// Boundaries: the two booleans per column and the per-table writable flag are
//   emitted; row counts come from the parallel vector, not the source.
// On failure first check: did a field name/order drift, or did row_counts stop
//   being read in parallel?
BOOST_AUTO_TEST_CASE(serialize_tables_two)
{
    TableCatalog catalog;

    auto links = named("links");
    links->m_writable = true;
    links->m_columns = {column("slug", "string", true, true),
                        column("clicks", "int", false, false)};
    auto events = named("click_events");
    events->m_writable = false;
    events->m_columns = {column("client_id_hash", "string", false, false)};

    BOOST_TEST(catalog.add(links));
    BOOST_TEST(catalog.add(events));

    const std::string expected =
        R"({"tables":[{"name":"links","rows":7,"writable":true,"columns":[)"
        R"({"name":"slug","type":"string","sortable":true,"searchable":true},)"
        R"({"name":"clicks","type":"int","sortable":false,"searchable":false}]},)"
        R"({"name":"click_events","rows":3,"writable":false,"columns":[)"
        R"({"name":"client_id_hash","type":"string","sortable":false,"searchable":false}]}]})";

    BOOST_TEST(serializeTablesJson(catalog, {7u, 3u}) == expected);
}

// Type: unit (db.rows serialization shape, empty page).
// Scenario: an empty page serializes with the full envelope shape and echoes
//   offset/limit/total.
// Boundaries: an empty rows array still produces columns and the three
//   integers.
// On failure first check: did serializeTableRowsJson change the empty form?
BOOST_AUTO_TEST_CASE(serialize_rows_empty_page)
{
    FakeTableSource source;
    source.m_name = "links";
    source.m_columns = {column("slug", "string", true, false),
                        column("clicks", "int", true, false)};

    TableRowsPage page;
    page.total = 0;

    const std::string expected =
        R"({"table":"links","columns":[{"name":"slug","type":"string"},)"
        R"({"name":"clicks","type":"int"}],"rows":[],"offset":5,"limit":10,"total":0})";

    BOOST_TEST(serializeTableRowsJson(source, page, 5, 10) == expected);
}

// Type: unit (db.rows serialization embeds escaped cells, T-51/T-52).
// Scenario: a row whose (source-escaped) object carries a quote, backslash and
//   newline is embedded verbatim and the result remains well-formed; a column
//   name containing a quote is escaped by the serializer itself.
// Boundaries: the serializer never re-escapes a row (double-escaping) and never
//   emits an unescaped quote in a column name.
// On failure first check: did the serializer start transforming rows_json, or
//   stop routing column names through jsonString?
BOOST_AUTO_TEST_CASE(serialize_rows_embeds_escaped_content)
{
    FakeTableSource source;
    source.m_name = "events";
    // A column name with a quote proves the serializer escapes its own output.
    source.m_columns = {column(R"(a"b)", "string", false, false)};

    TableRowsPage page;
    // One already-escaped object, exactly as a real source would produce via
    // jsonString: value is  a"b\c<newline>d .
    page.rows_json = {R"({"ua":"a\"b\\c\nd"})"};
    page.total = 1;

    const std::string expected =
        R"({"table":"events","columns":[{"name":"a\"b","type":"string"}],)"
        R"("rows":[{"ua":"a\"b\\c\nd"}],"offset":0,"limit":50,"total":1})";

    BOOST_TEST(serializeTableRowsJson(source, page, 0, 50) == expected);
}
