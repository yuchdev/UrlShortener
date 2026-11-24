#pragma once

#include <memory>

#include <url_shortener/tui/screen.hpp>

namespace url_shortener::tui
{

/**
 * @brief Creates the read-only Settings screen (D13, subtask 06.0/21).
 *
 * The screen renders the server's effective identity (version, backend names,
 * persistence, the session read-only flag) and the non-secret `shortener`
 * configuration served by `system.info`. It issues exactly one command,
 * `system.info`, on @ref IScreen::on_enter and on each manual refresh (`r`), and
 * never a mutating command; `q` returns to the Dashboard (handled by the router,
 * subtask 04). Every server-derived string is passed through
 * `sanitize_for_terminal` before width fitting (threat model T-110).
 *
 * @param context The screen context providing the async caller (for the one
 * `system.info` call), the session, and the theme/options. Its @ref
 * ScreenContext::caller and router are references owned by @ref App that must
 * outlive the returned screen; the screen captures the context by reference and
 * must not be used after @p context is destroyed.
 * @return An owning @ref IScreen for @ref ScreenId::settings; never null.
 */
std::unique_ptr<IScreen> make_settings_screen(ScreenContext& context);

}  // namespace url_shortener::tui
