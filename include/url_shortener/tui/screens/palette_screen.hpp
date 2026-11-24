#pragma once

#include <memory>
#include <vector>

#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/screen.hpp>

namespace url_shortener::tui
{

/**
 * @file palette_screen.hpp
 * @brief The Command Palette screen (D9, subtask 06.0/11): the `[:]` overlay
 * that makes every registry command reachable with no bespoke code (ADR 0003
 * Decision 2 rule 2).
 *
 * It filters @ref ICommandClient::commands through a @ref PaletteModel, renders
 * the ranked rows (name, summary, `read`/`write` tag, `(confirm)`/`(jobs)`
 * markers), and on Enter opens the @ref make_generic_command_screen flow for the
 * chosen command (form -> confirm -> result). The filter input sets
 * @ref ScreenContext::text_input_active so digit keys type into the query
 * instead of switching tabs (subtask 04). Every rendered command name / summary
 * / tag is sanitized before width fitting (threat model T-110); a write command
 * in a read-only session is shown dim and cannot be opened (ADR 0003 rule 5).
 */

/**
 * @brief Creates the Command Palette screen (D9).
 *
 * Builds a @ref PaletteModel from @p commands (the catalog
 * @ref ICommandClient::commands returned, passed in by the registering factory
 * so a screen never sees @ref ICommandClient itself, contract C8) and renders
 * it; the returned screen hosts the generic command flow inline when a command
 * is chosen and returns to the list when it closes. It issues no command itself
 * - every call goes through the hosted generic command screen's
 * @ref AsyncCaller - and never calls @ref ICommandClient::call directly
 * (contracts C1/C8).
 *
 * @param context The screen context providing the async caller, session, router
 * and options; its @ref ScreenContext::caller and router are references owned by
 * @ref App that must outlive the returned screen.
 * @param commands The command catalog to list; moved into the palette model.
 * The session's read-only flag (from @p context) decides which rows are
 * disabled (ADR 0003 rule 5).
 * @return An owning @ref IScreen for @ref ScreenId::palette; never null.
 */
std::unique_ptr<IScreen> make_palette_screen(ScreenContext& context,
                                             std::vector<CommandInfo> commands);

}  // namespace url_shortener::tui
