#pragma once

#include <memory>
#include <vector>

#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/screen.hpp>

namespace url_shortener::tui
{

/**
 * @file links_screen.hpp
 * @brief The Links tab screen (D3, subtask 06.0/12): the filter bar, status
 * radio and paged table of links layered over the generic `link.*` calls, plus
 * the D3 action keys (`Enter` detail, `n` new, `e` edit, `t` toggle, `d`
 * delete, `u` restore, `p` preview, `s` stats).
 *
 * The screen is a bespoke convenience over the generic command model (ADR 0003
 * Decision 2): it drives a @ref TableModel through @ref AsyncCaller with
 * `build_link_list_request`, hosts the generated `link.create` / `link.update`
 * form inline for new/edit, and routes every mutating submit (toggle / delete /
 * restore / create / update) through @ref run_mutation with the session's
 * read-only flag, so none reaches the caller without the confirm / capability
 * choke point (ADR 0003 rule 4, threat model T-116). It issues no command
 * through @ref ICommandClient directly (contract C8) and sanitizes every
 * server/stored string before display (threat model T-110).
 */

/**
 * @brief Creates the Links tab screen (D3).
 *
 * The screen owns a @ref LinksModel and a @ref TableModel over the D3 columns,
 * loads the first page on @ref IScreen::on_enter (and refreshes when the data
 * is older than ~30 s or on `r`), and renders the result through the reusable
 * table view. New/edit open the generic D5 form inline; toggle/delete/restore
 * submit through @ref run_mutation (delete with the typed-slug confirm);
 * preview and detail show the link JSON in an inline scrollable viewer. The
 * command catalog is passed in (a screen never sees @ref ICommandClient itself,
 * contract C8) so the screen can resolve the @ref CommandInfo each mutation
 * needs.
 *
 * @param context The screen context providing the async caller, session, router
 * and options; its @ref ScreenContext::caller and router are references owned
 * by
 * @ref App that must outlive the returned screen (captured by reference).
 * @param commands The command catalog, used to resolve the `link.*` commands
 * the actions invoke; moved in. The session's read-only flag (from @p context)
 * decides which write actions are available (ADR 0003 rule 5).
 * @return An owning @ref IScreen for @ref ScreenId::links; never null.
 */
std::unique_ptr<IScreen> make_links_screen(ScreenContext& context,
                                           std::vector<CommandInfo> commands);

}  // namespace url_shortener::tui
