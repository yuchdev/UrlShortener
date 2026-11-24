#pragma once

#include <memory>
#include <vector>

#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/screen.hpp>

namespace url_shortener::tui
{

/**
 * @file import_export_screen.hpp
 * @brief The Import / Export screen (D7, subtask 06.0/15): the `[4]` tab with
 * an Import sub-tab (`link.import`) and an Export sub-tab (`link.export`), both
 * job-backed. Implements the D11 edge `ImportExport -> Jobs (Start, job
 * accepted)`.
 *
 * The screen owns an @ref ImportModel and an @ref ExportModel (FTXUI-free; they
 * build the request `args`, contract C1/C9) and routes every submit through
 * @ref run_mutation with the session read-only flag and the shared
 * @ref ConfirmPolicy decision (ADR 0003 rule 4, threat model T-116):
 * `link.import` is a write (confirmed unless `--yes`, refused in read-only),
 * `link.export` is a read (submitted directly) whose `path` the server raises
 * to a write. On a job response the screen toasts the accepted job id, hands it
 * to the Jobs screen via @ref ScreenContext::focus_job_id and switches to the
 * Jobs tab. It issues no command through @ref ICommandClient directly (contract
 * C8), opens no file itself (a `path` is a command argument only, T-118) and
 * sanitizes every server/stored string before display (threat model T-110).
 */

/**
 * @brief Creates the Import / Export screen (D7).
 *
 * The screen loads no data on entry; it is a form. The command catalog is
 * passed in (a screen never sees @ref ICommandClient itself, contract C8) so it
 * can resolve the `link.import` / `link.export` @ref CommandInfo each submit
 * needs for the confirm / read-only gate; when a command is absent the submit
 * is refused with a toast. The inline-paste byte cap comes from
 * @ref TuiOptions::max_inline_bytes on @p context.
 *
 * @param context The screen context providing the async caller, session, router
 * and options; its @ref ScreenContext::caller and router are references owned
 * by
 * @ref App that must outlive the returned screen (captured by reference). The
 * screen sets @ref ScreenContext::focus_job_id when it hands a started job to
 * the Jobs screen.
 * @param commands The command catalog, used to resolve `link.import` /
 * `link.export`; moved in. The session's read-only flag (from @p context)
 * decides whether import Start and the export `path` field are available
 * (ADR 0003 rule 5).
 * @return An owning @ref IScreen for @ref ScreenId::import_export; never null.
 */
std::unique_ptr<IScreen> make_import_export_screen(
    ScreenContext& context, std::vector<CommandInfo> commands);

}  // namespace url_shortener::tui
