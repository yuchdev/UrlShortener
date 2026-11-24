#pragma once

#include <memory>

// Forward declarations keep this header free of Beast, storage and even the
// `app/table_source.hpp` include (01.0/13 include-hygiene guard): a
// `std::shared_ptr` to an incomplete @ref url_shortener::app::ITableSource and a
// reference to an incomplete @ref url_shortener::commands::JobRunner are all the
// declaration below needs. The implementation (`jobs_table_source.cpp`) pulls in
// the full definitions plus `core/utils.hpp` (which transitively includes Beast
// and must therefore never appear in a commands header).
namespace url_shortener
{
namespace app
{
class ITableSource;
}  // namespace app

namespace commands
{

class JobRunner;

/**
 * @brief Builds the read-only `jobs` logical `db.*` table over @p runner
 * (milestone C6 / decision Q7), exposing job *metadata only*.
 *
 * The returned source projects each retained @ref JobRecord onto the fixed,
 * literal column list `id, command, state, done, total, error_code, created_at,
 * finished_at` (threat model T-94). It deliberately never surfaces `args`,
 * `args_digest`, `result_json` (an export result can carry the whole link table
 * inline) or a failure *message* (which may echo caller-chosen argument names);
 * only the failure *code* reaches the `error_code` column. Every cell value is
 * `jsonString`-escaped by the source.
 *
 * Ownership / lifetime: @p runner is borrowed, not owned. The reference is
 * captured for the whole lifetime of the returned source, so @p runner must
 * outlive it. This holds by construction: the one process-wide @ref JobRunner
 * (decision D13) is wired into the catalog at composition time (subtask 20) and
 * outlives every table source registered against it.
 *
 * @param runner The process-wide job runner whose retained records the table
 * pages over.
 * @return A newly-constructed source as a `std::shared_ptr<app::ITableSource>`,
 * ready to register into a `TableCatalog`; never null.
 */
std::shared_ptr<app::ITableSource> make_jobs_table_source(
    const JobRunner& runner);

}  // namespace commands
}  // namespace url_shortener
