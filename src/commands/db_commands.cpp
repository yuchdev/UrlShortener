/**
 * @file db_commands.cpp
 * @brief The generic `db.tables` and `db.rows` command handlers over the
 * @ref url_shortener::app::TableCatalog allowlist (subtask 15). No SQL or
 * physical table name appears here: data flows only through
 * @ref url_shortener::app::ITableSource (milestone contract C6, threat model
 * T-51).
 */
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <url_shortener/app/app_error.hpp>
#include <url_shortener/app/table_source.hpp>
#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/browse_commands.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/error_codes.hpp>

namespace url_shortener::commands
{

namespace
{

///< Inclusive lower bound on the `db.rows` `limit` argument.
constexpr long long k_db_rows_min_limit = 1;

///< Inclusive upper bound on the `db.rows` `limit` argument; a value outside
///< `[k_db_rows_min_limit, k_db_rows_max_limit]` is rejected `invalid_field`
///< before any source call (threat model T-56).
constexpr long long k_db_rows_max_limit = 500;

///< Byte cap on the `db.rows` free-text `q` argument; a longer value is
///< rejected `invalid_field` before any source call.
constexpr std::size_t k_db_rows_max_query_length = 256;

/**
 * @brief Builds the argument schema for `db.rows` (catalog 3.2).
 *
 * Declares every bound on the descriptor so an out-of-range request is rejected
 * by the registry before the handler touches a source (threat model T-51/T-56):
 * `table` is a required string, `q` is capped at @ref
 * k_db_rows_max_query_length, `sort` is a free string (whitelisted against the
 * source's columns in the handler), `order` is the enum `asc`/`desc` (default
 * `asc`), `offset` is an integer `>= 0`, and `limit` is an integer in `1..500`.
 *
 * @return The ordered @ref ArgSpec list for the descriptor.
 */
std::vector<ArgSpec> make_db_rows_args()
{
    ArgSpec table;
    table.name = "table";
    table.type = ArgType::string;
    table.required = true;
    table.doc = "Name of the registered table to read.";

    ArgSpec q;
    q.name = "q";
    q.type = ArgType::string;
    q.max_length = k_db_rows_max_query_length;
    q.doc = "Free-text filter applied to the table's searchable columns.";

    ArgSpec sort;
    sort.name = "sort";
    sort.type = ArgType::string;
    sort.doc = "Name of a sortable column to order by.";

    ArgSpec order;
    order.name = "order";
    order.type = ArgType::enumeration;
    order.enum_values = {"asc", "desc"};
    order.default_value = "\"asc\"";
    order.doc = "Sort direction.";

    ArgSpec offset;
    offset.name = "offset";
    offset.type = ArgType::integer;
    offset.min = 0;
    offset.default_value = "0";
    offset.doc = "Zero-based index of the first row to return.";

    ArgSpec limit;
    limit.name = "limit";
    limit.type = ArgType::integer;
    limit.min = k_db_rows_min_limit;
    limit.max = k_db_rows_max_limit;
    limit.default_value = "50";
    limit.doc = "Maximum number of rows to return (1..500).";

    return {std::move(table),
            std::move(q),
            std::move(sort),
            std::move(order),
            std::move(offset),
            std::move(limit)};
}

/**
 * @brief Tests whether @p name is a sortable column of @p source.
 *
 * The whitelist is the source's own declared `sortable` columns, so a `sort`
 * argument can only ever name a column the source has opted to expose for
 * ordering (threat model T-51).
 *
 * @param source The source whose columns define the whitelist.
 * @param name The caller-supplied sort column.
 * @return `true` when @p source declares a `sortable` column equal to @p name.
 */
bool is_sortable_column(const app::ITableSource& source,
                        const std::string& name)
{
    for (const app::TableColumn& column : source.columns()) {
        if (column.sortable && column.name == name) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Handler for `db.tables`: list every registered table with its row
 * count.
 *
 * Walks the catalog in registration order, obtaining each source's row count;
 * the first `row_count` failure fails the whole call with `storage_failure`
 * (the source owns the fixed detail). On success the tables are serialized by
 * `app::serializeTablesJson`. Read command: no audit target.
 *
 * @param catalog The table allowlist to enumerate.
 * @return On success a @ref CommandResult carrying the serialized table list;
 * on a row-count failure the mapped error (`storage_failure`).
 */
CommandResult handle_db_tables(app::TableCatalog& catalog)
{
    CommandResult out;
    std::vector<std::size_t> row_counts;
    row_counts.reserve(catalog.all().size());
    for (const std::shared_ptr<app::ITableSource>& source : catalog.all()) {
        const app::Result<std::size_t> count = source->row_count();
        if (!count.ok() || !count.value) {
            out.error = make_error(from_app_error(count.error.code),
                                   count.error.detail);
            return out;
        }
        row_counts.push_back(*count.value);
    }
    out.result_json = app::serializeTablesJson(catalog, row_counts);
    return out;
}

/**
 * @brief Decodes the validated arguments into an `app::TableQuery`.
 *
 * Integer bounds (`offset >= 0`, `limit 1..500`), the `q` length cap and the
 * `order` enum were already enforced by the registry, so no value here can be
 * out of range. The `sort` value has been checked against the source's sortable
 * columns by the caller before this runs.
 *
 * @param args The validated argument view.
 * @return The decoded browse query.
 */
app::TableQuery decode_table_query(const ArgsView& args)
{
    app::TableQuery query;
    query.q = args.get_string("q");
    query.sort = args.get_string("sort");

    if (const std::optional<std::string> order = args.get_string("order")) {
        query.descending = (*order == "desc");
    }
    if (const std::optional<long long> offset = args.get_int("offset")) {
        query.offset = static_cast<std::size_t>(*offset);
    }
    if (const std::optional<long long> limit = args.get_int("limit")) {
        query.limit = static_cast<std::size_t>(*limit);
    }
    return query;
}

/**
 * @brief Handler for `db.rows`: resolve the table, validate `sort`, read,
 * serialize.
 *
 * Resolves `table` through `app::TableCatalog::find`; an unregistered name
 * returns `not_found` with the fixed message `table not found`, which never
 * echoes the requested name (threat model T-51). A `sort` that is not a
 * sortable column of the resolved source returns `invalid_field` naming only
 * `sort`. On success the query is built via @ref decode_table_query, the source
 * is read, and the page is serialized by `app::serializeTableRowsJson`; a
 * source failure is mapped to its command code. Read command: no audit target.
 *
 * @param catalog The allowlist the `table` argument is resolved against.
 * @param args The validated argument view (bounds already enforced).
 * @return On success a @ref CommandResult carrying the serialized page; on an
 * unknown table a fixed `not_found`; on a bad `sort` an `invalid_field`; on a
 * source failure the mapped error.
 */
CommandResult handle_db_rows(app::TableCatalog& catalog, const ArgsView& args)
{
    CommandResult out;

    // `table` is required, so validate_args guarantees a value here; the
    // defensive fallback keeps the handler total.
    const std::string table = args.get_string("table").value_or("");
    const app::ITableSource* source = catalog.find(table);
    if (source == nullptr) {
        // Fixed message: never echo the requested name (threat model T-51).
        out.error = make_error(CommandErrorCode::not_found, "table not found");
        return out;
    }

    if (const std::optional<std::string> sort = args.get_string("sort")) {
        if (!is_sortable_column(*source, *sort)) {
            // Name only the field, never the rejected value (contract C6).
            out.error = make_error(CommandErrorCode::invalid_field, "sort");
            return out;
        }
    }

    const app::TableQuery query = decode_table_query(args);
    const app::Result<app::TableRowsPage> result = source->rows(query);
    if (!result.ok() || !result.value) {
        out.error =
            make_error(from_app_error(result.error.code), result.error.detail);
        return out;
    }

    out.result_json = app::serializeTableRowsJson(
        *source, *result.value, query.offset, query.limit);
    return out;
}

}  // namespace

CommandDescriptor make_db_tables_descriptor(app::TableCatalog& catalog)
{
    CommandDescriptor descriptor;
    descriptor.name = "db.tables";
    descriptor.summary = "List the browsable tables with their row counts.";
    descriptor.permission = Permission::read;
    descriptor.kind = ExecutionKind::sync;
    descriptor.mutating = false;
    descriptor.supports_dry_run = false;
    descriptor.handler =
        [&catalog](CommandContext& /*context*/, const ArgsView& /*args*/)
    { return handle_db_tables(catalog); };
    return descriptor;
}

CommandDescriptor make_db_rows_descriptor(app::TableCatalog& catalog)
{
    CommandDescriptor descriptor;
    descriptor.name = "db.rows";
    descriptor.summary = "Read a page of rows from a browsable table.";
    descriptor.args = make_db_rows_args();
    descriptor.permission = Permission::read;
    descriptor.kind = ExecutionKind::sync;
    descriptor.mutating = false;
    descriptor.supports_dry_run = false;
    descriptor.handler =
        [&catalog](CommandContext& /*context*/, const ArgsView& args)
    { return handle_db_rows(catalog, args); };
    return descriptor;
}

}  // namespace url_shortener::commands
