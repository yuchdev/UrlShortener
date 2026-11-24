/**
 * @file app_exit_code.cpp
 * @brief Definition of the application-error-to-CLI-exit-code bridge.
 *
 * Kept out of `link_command_dispatch.cpp` so that dispatch unit never names
 * `app::AppErrorCode` (Task 04.0 exit criterion 2 greps for it). This is the
 * one CLI-side reader of the single shared error table (01.0/02).
 *
 * IMPORTANT (milestone contracts C4/C5): this translation unit must never start
 * the HTTP server, construct a Boost.Asio @c io_context, or touch
 * @c UriMapSingleton / `uri.txt`. It therefore includes nothing under
 * `url_shortener/http/` and no `boost/asio` headers.
 */
#include <url_shortener/cli/app_exit_code.hpp>

#include <url_shortener/commands/error_codes.hpp>

namespace url_shortener::cli
{

int ExitCodeForAppError(app::AppErrorCode code)
{
    // The single exit-code table lives in the commands layer (01.0/02); this is
    // its one CLI-side reader. `from_app_error` maps the app code to its command
    // code and `error_info_for(...).exit_code` yields the process exit code, so
    // there is no second mapping to drift from the registry's.
    return commands::error_info_for(commands::from_app_error(code)).exit_code;
}

}  // namespace url_shortener::cli
