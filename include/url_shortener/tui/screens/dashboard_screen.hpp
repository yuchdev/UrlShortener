#pragma once

#include <memory>
#include <vector>

#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/screen.hpp>

namespace url_shortener::tui
{

/**
 * @file dashboard_screen.hpp
 * @brief The Dashboard screen (D2, subtask 06.0/17): the `[1]` landing tab that
 * aggregates ordinary read commands into link counters, 24-hour redirect
 * figures, a "Top slugs" bar chart, "Recent changes", the running-jobs line and
 * the backend/cache status line.
 *
 * The screen renders a @ref DashboardModel whose six panels each load
 * independently, so one failing request degrades only its panel (success
 * criterion 2). It submits every request through the injected @ref AsyncCaller
 * with a cancellable token and an epoch stale-drop (a late response from a
 * superseded batch is discarded); a @ref PollTicker refreshes the batch every
 * 5 s while the tab is visible and is stopped on leave. Every server-derived
 * string is passed through `sanitize_for_terminal` before width fitting (threat
 * model T-110). The screen issues only read commands and never a mutating one
 * (success criterion 3).
 */

/**
 * @brief Creates the Dashboard screen (D2).
 *
 * @param context The screen context providing the async caller, session, router
 * and options; its @ref ScreenContext::caller and router are references owned
 * by
 * @ref App that must outlive the returned screen (captured by reference). On
 * `Enter` the screen asks the router to jump to the related tab (Links/Jobs).
 * @param commands The command catalog, used only to feature-detect a
 * `stats.summary` command (which switches the model to exact redirect metrics);
 * a screen never sees @ref ICommandClient itself (contract C8). Moved in.
 * @return An owning @ref IScreen for @ref ScreenId::dashboard; never null.
 */
std::unique_ptr<IScreen> make_dashboard_screen(
    ScreenContext& context, std::vector<CommandInfo> commands);

}  // namespace url_shortener::tui
