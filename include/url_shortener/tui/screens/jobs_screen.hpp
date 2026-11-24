#pragma once

#include <memory>
#include <vector>

#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/screen.hpp>

namespace url_shortener::tui
{

/**
 * @file jobs_screen.hpp
 * @brief The Jobs screen (D8, subtask 06.0/16): the `[5]` tab that lists jobs
 * (`job.list`), polls them on a 1 s timer, cancels (`job.cancel`) and opens a
 * job's full envelope detail (`job.get`, D11 edge `Jobs -> JobDetail`).
 *
 * The screen renders a @ref JobsModel through the reusable `TableView` (every
 * cell sanitized, T-110), auto-refreshes with a @ref PollTicker whose cadence
 * follows a @ref PollSchedule (ADR 0003 rule 3), and routes the one mutating
 * action (`job.cancel`) through @ref run_mutation with the session read-only
 * flag (ADR 0003 rule 4, threat model T-116). Every command goes through the
 * injected @ref AsyncCaller; the ticker thread only posts a UI-thread tick and
 * never calls @ref ICommandClient (contracts C1/C8).
 */

/**
 * @brief Creates the Jobs screen (D8).
 *
 * @param context The screen context providing the async caller, session, router
 * and options; its @ref ScreenContext::caller and router are references owned
 * by
 * @ref App that must outlive the returned screen (captured by reference). The
 * screen also reads @ref ScreenContext::focus_job_id on entry to land on a
 * freshly-started job handed over from Import/Export.
 * @param commands The command catalog, used only to recover the `job.cancel`
 * descriptor's base permission for the read-only/confirm gate (a screen never
 * sees @ref ICommandClient itself, contract C8); moved in. When the catalog
 * omits `job.cancel` a fail-closed `write` descriptor is assumed.
 * @return An owning @ref IScreen for @ref ScreenId::jobs; never null.
 */
std::unique_ptr<IScreen> make_jobs_screen(ScreenContext& context,
                                          std::vector<CommandInfo> commands);

}  // namespace url_shortener::tui
