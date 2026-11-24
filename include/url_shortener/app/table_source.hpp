#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <url_shortener/app/app_error.hpp>

namespace url_shortener
{
namespace app
{

/**
 * @brief One column of a logical `db.*` table as declared by its
 * @ref ITableSource.
 *
 * Purely descriptive schema metadata: it names a column, its catalog type, and
 * whether the generic `db.rows` handler may sort or free-text search on it. The
 * handler consults @ref sortable to decide whether a caller-supplied `sort`
 * argument is admissible (the whitelist is the set of columns with
 * `sortable == true`), so this struct is the single authority on what a table
 * exposes. It carries no storage or SQL knowledge.
 */
struct TableColumn
{
    ///< Column name as it appears in each serialized row object and in the
    ///< `columns` metadata. Must be a stable, non-secret identifier chosen by
    ///< the source.
    std::string name;

    ///< Catalog type of the column, one of the fixed tokens `"string"`,
    ///< `"int"`, `"bool"`, `"timestamp"`; emitted verbatim in the `columns`
    ///< metadata and used by the TUI for rendering only.
    std::string type;

    ///< `true` when `db.rows` may order by this column; a `sort` argument that
    ///< does not name a `sortable` column is rejected `invalid_field` before
    ///< any source call (threat model T-51).
    bool sortable = false;

    ///< `true` when this column participates in the free-text `q` filter;
    ///< purely informational to the generic handler, which passes `q` through
    ///< unchanged for the source to interpret.
    bool searchable = false;
};

/**
 * @brief A validated, bounded browse query handed to @ref ITableSource::rows.
 *
 * Every field has already been range- and length-checked by the `db.rows`
 * command (via its @ref ArgSpec rows) before this struct is built, so a source
 * receives only in-bounds values: `limit` is in `1..500`, `offset >= 0`, `q` is
 * at most 256 bytes, and `sort`, when set, is guaranteed to name a `sortable`
 * column of the source. The source never has to re-derive these bounds and must
 * never treat any field as SQL text.
 */
struct TableQuery
{
    ///< Optional free-text filter; absent means no text filter. Already length-
    ///< bounded by the command. The source decides which columns it matches.
    std::optional<std::string> q;

    ///< Optional sort column; absent means the source's natural order. When
    ///< present it is guaranteed to equal the `name` of one of the source's
    ///< `sortable` columns (validated by the handler), never arbitrary text.
    std::optional<std::string> sort;

    ///< `true` sorts descending, `false` ascending; honored only when @ref sort
    ///< is present.
    bool descending = false;

    ///< Zero-based index of the first row to return after filtering and
    ///< sorting; a value at or beyond the total yields an empty page.
    std::size_t offset = 0;

    ///< Maximum number of rows to return; already validated to `1..500` by the
    ///< command, defaulting to 50.
    std::size_t limit = 50;
};

/**
 * @brief One filtered, counted window of rows returned by
 * @ref ITableSource::rows.
 *
 * `total` counts every row matching the query's filters before the
 * `offset`/`limit` window was applied, so it is stable across pages.
 */
struct TableRowsPage
{
    ///< The requested window of rows. Each element is one complete, already
    ///< `jsonString`-escaped JSON object (e.g. `{"slug":"x","clicks":3}`): the
    ///< source is solely responsible for escaping every cell value, so the
    ///< generic serializer concatenates these verbatim and never touches
    ///< untrusted text (milestone contract C6, threat model T-51/T-52).
    std::vector<std::string> rows_json;

    ///< Number of rows matching the query's filters before the window was
    ///< applied; independent of `offset`/`limit`.
    std::size_t total = 0;
};

/**
 * @brief Port behind one logical `db.*` table (milestone contract C6).
 *
 * A source owns the meaning of its columns and the projection of its rows; the
 * generic `db.tables`/`db.rows` handlers never learn a physical table name and
 * never build SQL. A source must expose only non-secret columns and must
 * escape every cell value it places in @ref TableRowsPage::rows_json. Concrete
 * sources (`links`, `click_events`, later `jobs`) are registered into a
 * @ref TableCatalog, which is the allowlist: an unregistered table cannot be
 * named, listed or read. Thread-safety follows the backing repository.
 */
class ITableSource
{
public:
    /**
     * @brief Virtual destructor; implementations may be destroyed through a
     * base `ITableSource*`.
     */
    virtual ~ITableSource() = default;

    /**
     * @brief The table's logical name, the key `db.rows`/`db.tables` address it
     * by.
     * @return A stable identifier matching `[a-z][a-z0-9_]*`; @ref
     * TableCatalog::add rejects a source whose name is malformed, duplicated or
     * a forbidden auth-table name.
     */
    virtual const std::string& name() const = 0;

    /**
     * @brief Whether the table has a typed write command (informational only).
     * @return `true` when a typed mutation command exists for this table (e.g.
     * `links` has `link.create`); purely advisory for the TUI. There is no
     * write path through `db.*` regardless of this value.
     */
    virtual bool writable() const = 0;

    /**
     * @brief The table's declared columns, in display order.
     * @return The column metadata; the `sortable` subset is the sole whitelist
     * `db.rows` checks a caller's `sort` argument against.
     */
    virtual const std::vector<TableColumn>& columns() const = 0;

    /**
     * @brief The total number of rows currently in the table.
     * @return `Result<std::size_t>` populated on success; on failure a
     * `storage_failure` (the backing store failed). A failure here fails the
     * whole `db.tables` call.
     */
    virtual Result<std::size_t> row_count() const = 0;

    /**
     * @brief Returns a filtered, counted page of rows.
     * @param query A bounded, pre-validated browse query (see @ref TableQuery);
     * the source may trust its bounds and the admissibility of its `sort`.
     * @return `Result<TableRowsPage>` populated on success; on failure a
     * `storage_failure` (the backing store failed). The source must escape
     * every cell value with `jsonString` before placing it in `rows_json`.
     */
    virtual Result<TableRowsPage> rows(const TableQuery& query) const = 0;
};

/**
 * @brief The set of logical tables that exist for `db.*`: the allowlist, by
 * construction (milestone contract C6).
 *
 * A table is browsable if and only if it was successfully registered here, so
 * an unregistered name cannot be listed or read. @ref add additionally refuses
 * malformed names and an explicit forbidden set of auth tables, so a source can
 * never surface credentials or audit rows. The catalog is built once at startup
 * and then read concurrently by command handlers; registration is not
 * thread-safe, reads after the build are.
 */
class TableCatalog
{
public:
    /**
     * @brief Registers one source, enforcing the allowlist's guards.
     *
     * Ownership: the catalog takes shared ownership of @p source and keeps it
     * for its own lifetime; a successfully added source must therefore remain
     * valid as long as the catalog is used.
     *
     * @param source The source to register; its @ref ITableSource::name is the
     * key callers address.
     * @return `true` when the source was added. `false`, leaving the catalog
     * unchanged, when @p source is null; when its name is malformed (empty,
     * does not match `[a-z][a-z0-9_]*`, or exceeds the name-length cap); when
     * the name is, compared case-insensitively, one of the forbidden auth
     * tables `{app_users, auth_sessions, service_clients, auth_audit_log,
     * user_credentials}`; or when a source with the same name is already
     * registered.
     */
    bool add(std::shared_ptr<ITableSource> source);

    /**
     * @brief Looks up a registered source by exact name.
     * @param name Candidate table name (untrusted; compared byte-for-byte,
     * never interpreted as SQL).
     * @return A non-owning pointer to the registered source, valid for the
     * catalog's lifetime; `nullptr` when no source has that exact name
     * (including every unregistered, forbidden or malformed name).
     */
    const ITableSource* find(std::string_view name) const;

    /**
     * @brief All registered sources in registration order.
     * @return A reference to the internal list, valid for the catalog's
     * lifetime; parallel in order to the `row_counts` @ref serializeTablesJson
     * expects.
     */
    const std::vector<std::shared_ptr<ITableSource>>& all() const;

private:
    ///< The registered sources in insertion order; shared ownership is held for
    ///< the catalog's lifetime.
    std::vector<std::shared_ptr<ITableSource>> m_sources_;
};

/**
 * @brief Serializes a catalog's tables to the `db.tables` result object.
 *
 * Emits
 * `{"tables":[{"name","rows","writable","columns":[{"name","type","sortable","searchable"}]}]}`
 * over @p catalog's sources in registration order. Column names and types are
 * `jsonString`-escaped; `name` values are already constrained to
 * `[a-z][a-z0-9_]*` by @ref TableCatalog::add.
 *
 * @param catalog The catalog whose sources are described.
 * @param row_counts One row count per source, parallel to
 * @ref TableCatalog::all (same size and order); the handler fills it by calling
 * each source's @ref ITableSource::row_count. A shorter vector is treated as a
 * count of 0 for the missing entries (defensive; the handler always supplies a
 * full vector).
 * @return The canonical `db.tables` JSON object.
 */
std::string serializeTablesJson(const TableCatalog& catalog,
                                const std::vector<std::size_t>& row_counts);

/**
 * @brief Serializes one page of rows to the `db.rows` result object.
 *
 * Emits
 * `{"table","columns":[{"name","type"}],"rows":[...],"offset","limit","total"}`.
 * The `table` name and column metadata are `jsonString`-escaped; each element
 * of `rows` is embedded verbatim from @ref TableRowsPage::rows_json (the source
 * has already escaped every cell), so the serializer never concatenates
 * untrusted text (threat model T-51/T-52).
 *
 * @param source The source the page came from; supplies the `table` name and
 * `columns` metadata.
 * @param page The rows window and total to serialize.
 * @param offset The query offset echoed into the result.
 * @param limit The query limit echoed into the result.
 * @return The canonical `db.rows` JSON object.
 */
std::string serializeTableRowsJson(const ITableSource& source,
                                   const TableRowsPage& page,
                                   std::size_t offset,
                                   std::size_t limit);

}  // namespace app
}  // namespace url_shortener
