/**
 * @file app_exit_code.hpp
 * @brief Bridge from an application error code to the CLI process exit code.
 *
 * Declared in its own small header (not in `link_command_dispatch.hpp`'s body)
 * and defined in its own translation unit (`src/cli/app_exit_code.cpp`) so the
 * `link <verb>` dispatch unit never has to name `app::AppErrorCode`: Task 04.0
 * exit criterion 2 greps `src/cli/link_command_dispatch.cpp` for `AppErrorCode`
 * and requires no match. Callers that only include `link_command_dispatch.hpp`
 * (such as `src/main.cpp` and `tests/unit/cli/04_cli_exit_codes.cpp`) still see
 * this declaration because that header includes this one.
 *
 * Like the other CLI headers, this one is decoupled from the HTTP server: it
 * pulls in nothing under `url_shortener/http/`, no Boost.Asio/Beast, and never
 * starts the network server or touches `uri.txt`/`UriMapSingleton` (milestone
 * contracts C4/C5).
 */
#pragma once

#include <url_shortener/app/app_error.hpp>

namespace url_shortener::cli
{
/**
 * @brief Map an application error code to the CLI process exit code.
 *
 * A one-line reader of the single shared error table (01.0/02): it maps @p code
 * to its @ref url_shortener::commands::CommandErrorCode via `from_app_error` and
 * returns that row's `exit_code`. There is no second exit-code mapping in the
 * CLI, so a `link <verb>` failure and the equivalent `cmd` failure always share
 * one exit code, and a script can branch on `$?` alone. See
 * `commands::all_error_infos()` for the authoritative code-to-exit-code table.
 *
 * @param code Application error code carried by a failed service @c Result.
 * @return Non-negative process exit code from the shared table (0 only for
 * @c none).
 */
int ExitCodeForAppError(app::AppErrorCode code);

}  // namespace url_shortener::cli
