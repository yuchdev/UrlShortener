#pragma once

#include <memory>
#include <vector>

#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/screen.hpp>

namespace url_shortener::tui
{

/**
 * @file tables_screen.hpp
 * @brief The Database tables browser screen (D6, subtask 06.0/14): the `[3]`
 * tab with a left pane of logical tables (row counts and the read-only note)
 * and a right pane paging a sortable, searchable `TableView` over `db.rows`.
 *
 * The screen drives a @ref TablesModel through @ref AsyncCaller: `db.tables`
 * populates the left pane; selecting a table issues `db.rows` page 1. It
 * renders the row JSON of a selected row inline through a @ref
 * ResultViewerModel (D11 edge `Tables -> RowJson`) and, only for a `writable`
 * table, hosts the generated `link.create` form inline for insert (D11 edge
 * `Tables -> LinkForm`). Every cell and the row-JSON view are sanitized at the
 * render sink (T-110); the one mutating submit (`link.create`) routes through
 * the form's @ref run_mutation choke point with the session read-only flag (ADR
 * 0003 rule 4, threat model T-116). The screen issues no command through @ref
 * ICommandClient directly (contract C8) and sends only `db.tables`, `db.rows`
 * and `link.create` (success criterion 3).
 */

/**
 * @brief Creates the Database tables browser screen (D6).
 *
 * The screen owns a @ref TablesModel and, per selected table, a @ref TableModel
 * over the table's derived columns; it loads `db.tables` on
 * @ref IScreen::on_enter (refreshing when stale) and drops every in-flight call
 * on @ref IScreen::on_leave. The command catalog is passed in (a screen never
 * sees @ref ICommandClient itself, contract C8) so the screen can resolve the
 * `link.create` @ref CommandInfo the insert form needs.
 *
 * @param context The screen context providing the async caller, session, router
 * and options; its @ref ScreenContext::caller and router are references owned
 * by
 * @ref App that must outlive the returned screen (captured by reference).
 * @param commands The command catalog, used to resolve `link.create` for the
 * insert form; moved in. The session's read-only flag (from @p context) decides
 * whether insert is available (ADR 0003 rule 5).
 * @return An owning @ref IScreen for @ref ScreenId::tables; never null.
 */
std::unique_ptr<IScreen> make_tables_screen(ScreenContext& context,
                                            std::vector<CommandInfo> commands);

}  // namespace url_shortener::tui
