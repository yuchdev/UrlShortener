#define BOOST_TEST_MODULE CommandsDbCommandsTest
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/app/app_error.hpp"
#include "url_shortener/app/table_source.hpp"
#include "url_shortener/commands/browse_commands.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"

using url_shortener::app::AppError;
using url_shortener::app::AppErrorCode;
using url_shortener::app::ITableSource;
using url_shortener::app::Result;
using url_shortener::app::serializeTableRowsJson;
using url_shortener::app::TableCatalog;
using url_shortener::app::TableColumn;
using url_shortener::app::TableQuery;
using url_shortener::app::TableRowsPage;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::make_db_rows_descriptor;
using url_shortener::commands::make_db_tables_descriptor;

namespace
{

// A fake ITableSource that records how often row_count()/rows() were called and
// the exact query rows() received, and returns either a configured value or a
// forced error. The recording members are mutable because the port methods are
// const; each test owns a fresh instance, so the counters start at zero.
class FakeTableSource : public ITableSource
{
public:
    std::string m_name;
    bool m_writable = false;
    std::vector<TableColumn> m_columns;
    std::size_t m_count = 0;
    TableRowsPage m_page;
    AppError m_count_error;
    AppError m_rows_error;

    mutable int count_calls = 0;
    mutable int rows_calls = 0;
    mutable TableQuery last_query;

    const std::string& name() const override { return m_name; }
    bool writable() const override { return m_writable; }
    const std::vector<TableColumn>& columns() const override
    {
        return m_columns;
    }

    Result<std::size_t> row_count() const override
    {
        ++count_calls;
        Result<std::size_t> result;
        if (m_count_error.code != AppErrorCode::none) {
            result.error = m_count_error;
            return result;
        }
        result.value = m_count;
        return result;
    }

    Result<TableRowsPage> rows(const TableQuery& query) const override
    {
        ++rows_calls;
        last_query = query;
        Result<TableRowsPage> result;
        if (m_rows_error.code != AppErrorCode::none) {
            result.error = m_rows_error;
            return result;
        }
        result.value = m_page;
        return result;
    }
};

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

// Builds a "links" source with slug (sortable+searchable) and clicks (sortable)
// columns, used by most db.rows cases.
std::shared_ptr<FakeTableSource> links_source()
{
    auto source = std::make_shared<FakeTableSource>();
    source->m_name = "links";
    source->m_writable = true;
    source->m_columns = {column("slug", "string", true, true),
                         column("clicks", "int", true, false)};
    return source;
}

// A read-only User caller: db.* requires only Permission::read.
CallerContext user_caller()
{
    CallerContext caller;
    caller.principal = "test-user";
    caller.transport = "test";
    caller.control_set = ControlSet::User;
    return caller;
}

CommandRequest request(std::string command, std::string args_json)
{
    CommandRequest req;
    req.id = "client-1";
    req.command = std::move(command);
    req.args_json = std::move(args_json);
    return req;
}

// Bundles a catalog, a registry carrying both db.* descriptors, and the fake
// source handles the test keeps to inspect call counters. The catalog is
// declared before the registry so it outlives the handlers that capture it.
struct Harness
{
    TableCatalog catalog;
    CommandRegistry registry;

    Harness()
    {
        registry.add(make_db_tables_descriptor(catalog));
        registry.add(make_db_rows_descriptor(catalog));
    }

    std::shared_ptr<FakeTableSource> add(
        std::shared_ptr<FakeTableSource> source)
    {
        catalog.add(source);
        return source;
    }

    CommandResponse tables()
    {
        return registry.execute(user_caller(), request("db.tables", "{}"));
    }

    CommandResponse rows(std::string args_json)
    {
        return registry.execute(user_caller(),
                                request("db.rows", std::move(args_json)));
    }
};

}  // namespace

// Type: unit (db.tables lists the registered fakes with counts and types).
// Scenario: two registered sources serialize to the full db.tables shape with
//   their configured row counts and column types, in registration order.
// Boundaries: db.tables reads exactly the catalog, nothing more.
// On failure first check: did the handler stop calling row_count per source or
//   reorder the catalog?
BOOST_AUTO_TEST_CASE(db_tables_lists_registered_fakes)
{
    Harness h;
    auto links = h.add(links_source());
    links->m_count = 7;
    auto events = std::make_shared<FakeTableSource>();
    events->m_name = "click_events";
    events->m_columns = {column("client_id_hash", "string", false, false)};
    events->m_count = 3;
    h.add(events);

    const CommandResponse response = h.tables();

    BOOST_TEST(response.ok);
    BOOST_TEST(response.result_json.find(R"("name":"links")")
               != std::string::npos);
    BOOST_TEST(response.result_json.find(R"("rows":7)") != std::string::npos);
    BOOST_TEST(response.result_json.find(R"("name":"click_events")")
               != std::string::npos);
    BOOST_TEST(response.result_json.find(R"("rows":3)") != std::string::npos);
    BOOST_TEST(response.result_json.find(R"("type":"int")")
               != std::string::npos);
    BOOST_TEST(links->count_calls == 1);
}

// Type: unit (db.tables row-count failure -> storage_failure).
// Scenario: a source whose row_count fails fails the whole db.tables call with
//   storage_failure.
// Boundaries: one failing source is enough; the result is not a partial list.
// On failure first check: did the handler swallow the row_count error?
BOOST_AUTO_TEST_CASE(db_tables_row_count_failure_is_storage_failure)
{
    Harness h;
    auto links = h.add(links_source());
    links->m_count_error = AppError {AppErrorCode::storage_failure, "backend"};

    const CommandResponse response = h.tables();

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::storage_failure));
}

// Type: unit (db.rows happy path: paging, sort desc, q passthrough).
// Scenario: db.rows on a registered table decodes offset/limit/sort/order/q
// into
//   the query, calls rows() once, and serializes the page verbatim.
// Boundaries: order "desc" flips descending; each field lands on its target.
// On failure first check: did decode_table_query misroute a field, or did the
//   handler reformat the page?
BOOST_AUTO_TEST_CASE(db_rows_happy_path)
{
    Harness h;
    auto links = h.add(links_source());
    links->m_page.rows_json = {R"({"slug":"a"})", R"({"slug":"b"})"};
    links->m_page.total = 2;

    const CommandResponse response =
        h.rows(R"({"table":"links","q":"foo","sort":"slug","order":"desc",)"
               R"("offset":3,"limit":7})");

    BOOST_TEST(response.ok);
    BOOST_TEST(links->rows_calls == 1);
    BOOST_TEST((links->last_query.q.value_or("") == "foo"));
    BOOST_TEST((links->last_query.sort.value_or("") == "slug"));
    BOOST_TEST(links->last_query.descending);
    BOOST_TEST(links->last_query.offset == 3u);
    BOOST_TEST(links->last_query.limit == 7u);
    BOOST_TEST(response.result_json
               == serializeTableRowsJson(*links, links->m_page, 3, 7));
}

// Type: unit (db.rows default sort direction is ascending).
// Scenario: omitting order leaves descending false (asc) and reaches the
// source. Boundaries: the order default is asc, not desc. On failure first
// check: did the order default flip to desc?
BOOST_AUTO_TEST_CASE(db_rows_default_order_is_ascending)
{
    Harness h;
    auto links = h.add(links_source());

    const CommandResponse response =
        h.rows(R"({"table":"links","sort":"slug"})");

    BOOST_TEST(response.ok);
    BOOST_TEST(!links->last_query.descending);
}

// Type: unit (db.rows default limit/offset reach the source).
// Scenario: db.rows with only table decodes to the struct defaults (offset 0,
//   limit 50) and calls rows once.
// Boundaries: an absent offset/limit keeps the TableQuery default.
// On failure first check: did a default drift from TableQuery?
BOOST_AUTO_TEST_CASE(db_rows_defaults_reach_source)
{
    Harness h;
    auto links = h.add(links_source());

    const CommandResponse response = h.rows(R"({"table":"links"})");

    BOOST_TEST(response.ok);
    BOOST_TEST(links->rows_calls == 1);
    BOOST_TEST(links->last_query.offset == 0u);
    BOOST_TEST(links->last_query.limit == 50u);
    BOOST_TEST(!links->last_query.sort.has_value());
}

// Type: unit (unregistered / forbidden / malformed table -> not_found, T-51).
// Scenario: a name that is not a registered source -- an ordinary unknown name,
//   a forbidden auth table, a case variant of a registered name, and an
//   injection-shaped name -- all return not_found with the fixed message and
//   never reach any source. An empty table name passes the ArgSpec (no
//   non-empty rule) and likewise resolves to not_found.
// Boundaries: find() matches exactly; the response never echoes the name.
// On failure first check: did find() start matching loosely, or did the handler
//   build a message from the argument?
BOOST_AUTO_TEST_CASE(db_rows_unknown_table_is_not_found_without_echo)
{
    const std::vector<std::string> names = {
        "users",
        "app_users",
        "auth_sessions",
        "LINKS",
        "links; DROP",
        "",
    };

    for (const std::string& name : names) {
        Harness h;
        auto links = h.add(links_source());

        CommandResponse response;
        if (name.empty()) {
            response = h.rows(R"({"table":""})");
        }
        else {
            response = h.rows(R"({"table":")" + name + R"("})");
        }

        BOOST_TEST(!response.ok, "name should not resolve: " << name);
        BOOST_TEST((response.error.code == CommandErrorCode::not_found));
        BOOST_TEST(response.error.message == "table not found");
        // The fixed message never echoes the requested name.
        if (!name.empty()) {
            BOOST_TEST(response.error.message.find(name) == std::string::npos);
        }
        BOOST_TEST(links->rows_calls == 0);
    }
}

// Type: unit (unknown/non-sortable sort column -> invalid_field, T-51).
// Scenario: a sort naming a column that is not declared sortable (and one that
//   does not exist at all) is rejected invalid_field naming only "sort"; the
//   source is never read.
// Boundaries: the whitelist is the source's sortable columns; the rejected
//   value is never echoed.
// On failure first check: did is_sortable_column admit a non-sortable column?
BOOST_AUTO_TEST_CASE(db_rows_bad_sort_column_is_invalid_field)
{
    Harness h;
    auto links = h.add(links_source());
    // Add a non-sortable column to prove "declared but not sortable" is
    // rejected.
    links->m_columns.push_back(column("target", "string", false, true));

    const CommandResponse unknown =
        h.rows(R"({"table":"links","sort":"nonexistent"})");
    BOOST_TEST(!unknown.ok);
    BOOST_TEST((unknown.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(unknown.error.message == "sort");
    BOOST_TEST(unknown.error.message.find("nonexistent") == std::string::npos);

    const CommandResponse not_sortable =
        h.rows(R"({"table":"links","sort":"target"})");
    BOOST_TEST(!not_sortable.ok);
    BOOST_TEST((not_sortable.error.code == CommandErrorCode::invalid_field));

    BOOST_TEST(links->rows_calls == 0);
}

// Type: unit (a sortable column is accepted and reaches the source).
// Scenario: sort naming a declared sortable column passes and the query carries
//   it to the source.
// Boundaries: "clicks" is sortable; it must not be rejected.
// On failure first check: did the whitelist stop including every sortable
//   column?
BOOST_AUTO_TEST_CASE(db_rows_sortable_column_accepted)
{
    Harness h;
    auto links = h.add(links_source());

    const CommandResponse response =
        h.rows(R"({"table":"links","sort":"clicks"})");

    BOOST_TEST(response.ok);
    BOOST_TEST(links->rows_calls == 1);
    BOOST_TEST((links->last_query.sort.value_or("") == "clicks"));
}

// Type: unit (out-of-bounds limit/offset rejected before the source, T-56).
// Scenario: limit 0, limit 501, offset -1 and q of 257 bytes each fail
//   invalid_field and never reach the source (a counting fake proves it).
// Boundaries: the ArgSpec bounds (1..500, >=0, <=256) gate every value before
//   the handler.
// On failure first check: which bound drifted, or did validation stop
//   preceding the handler?
BOOST_AUTO_TEST_CASE(db_rows_out_of_bounds_rejected_before_source)
{
    const std::vector<std::string> invalid = {
        R"({"table":"links","limit":0})",
        R"({"table":"links","limit":501})",
        R"({"table":"links","offset":-1})",
        R"({"table":"links","q":")" + std::string(257, 'a') + R"("})",
    };

    for (const std::string& args : invalid) {
        Harness h;
        auto links = h.add(links_source());

        const CommandResponse response = h.rows(args);

        BOOST_TEST(!response.ok, "should be rejected: " << args);
        BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
        BOOST_TEST(links->rows_calls == 0);
    }
}

// Type: unit (limit/q at their caps accepted).
// Scenario: limit 500 and q of exactly 256 bytes are accepted and reach the
//   source.
// Boundaries: 500 and 256 are the last accepted values.
// On failure first check: did a cap drift below 500/256?
BOOST_AUTO_TEST_CASE(db_rows_boundary_values_accepted)
{
    {
        Harness h;
        auto links = h.add(links_source());
        const CommandResponse response =
            h.rows(R"({"table":"links","limit":500})");
        BOOST_TEST(response.ok);
        BOOST_TEST(links->last_query.limit == 500u);
    }
    {
        Harness h;
        auto links = h.add(links_source());
        const std::string q(256, 'a');
        const CommandResponse response =
            h.rows(R"({"table":"links","q":")" + q + R"("})");
        BOOST_TEST(response.ok);
        BOOST_TEST((links->last_query.q.value_or("") == q));
    }
}

// Type: unit (missing table / unknown arg / bad order enum rejected).
// Scenario: omitting the required table, passing an undeclared argument, and a
//   bad order enum each fail invalid_field without reaching any source.
// Boundaries: the schema is closed and table is required.
// On failure first check: did table stop being required, or the schema open up?
BOOST_AUTO_TEST_CASE(db_rows_schema_violations_rejected)
{
    {
        Harness h;
        auto links = h.add(links_source());
        const CommandResponse response = h.rows(R"({"limit":5})");
        BOOST_TEST(!response.ok);
        BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
        BOOST_TEST(links->rows_calls == 0);
    }
    {
        Harness h;
        h.add(links_source());
        const CommandResponse response =
            h.rows(R"({"table":"links","bogus":1})");
        BOOST_TEST(!response.ok);
        BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
    }
    {
        Harness h;
        h.add(links_source());
        const CommandResponse response =
            h.rows(R"({"table":"links","order":"up"})");
        BOOST_TEST(!response.ok);
        BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
        BOOST_TEST(response.error.message.find("not_in_enum")
                   != std::string::npos);
    }
}

// Type: unit (source read failure -> storage_failure).
// Scenario: a registered source whose rows() fails surfaces storage_failure.
// Boundaries: the source error maps through from_app_error to its command code.
// On failure first check: did the handler stop mapping result.error?
BOOST_AUTO_TEST_CASE(db_rows_source_failure_is_storage_failure)
{
    Harness h;
    auto links = h.add(links_source());
    links->m_rows_error = AppError {AppErrorCode::storage_failure, "backend"};

    const CommandResponse response = h.rows(R"({"table":"links"})");

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::storage_failure));
    BOOST_TEST(links->rows_calls == 1);
}

// Type: unit (read-only caller allowed for both db.* commands).
// Scenario: a ControlSet::User caller may run db.tables and db.rows.
// Boundaries: both descriptors require only read permission.
// On failure first check: did a descriptor permission rise above read?
BOOST_AUTO_TEST_CASE(read_only_user_allowed)
{
    Harness h;
    h.add(links_source());

    BOOST_TEST(h.tables().ok);
    BOOST_TEST(h.rows(R"({"table":"links"})").ok);
}
