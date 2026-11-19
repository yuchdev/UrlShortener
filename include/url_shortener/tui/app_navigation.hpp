/**
 * @file app_navigation.hpp
 * @brief The @ref url_shortener::tui::App navigation primitives, extracted from
 * `App::run` so they can be exercised headlessly (BUG-2, 06.0/20 smoke).
 *
 * `App::run` owns an `ftxui::ScreenInteractive` and its `Loop`, which need a
 * real terminal and so cannot run in CI. The two pieces of logic the global
 * hotkeys and the per-screen text-input focus depend on - the active-screen
 * hand-off (@ref sync_active_screen) and the global key dispatch
 * (@ref handle_global_key) - are therefore free functions here: `App::run`
 * calls them from its FTXUI render/event lambdas, and the app-level navigation
 * test drives them directly over the real registered screens on one shared
 * @ref ScreenContext.
 */
#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <ftxui/component/event.hpp>
#include <url_shortener/tui/router.hpp>
#include <url_shortener/tui/screen.hpp>
#include <url_shortener/tui/screen_id.hpp>

namespace url_shortener::tui
{

/**
 * @brief Fires the @ref IScreen::on_leave / @ref IScreen::on_enter hand-off
 * when the active tab changed and makes @ref ScreenContext::text_input_active
 * reflect only the screen now active (BUG-2, 06.0/20 smoke).
 *
 * Every screen shares one @ref ScreenContext, so an off-screen screen whose
 * default focus is a text field (e.g. Import/Export's paste area) must not be
 * able to leave the shared `text_input_active` flag `true` - that flag drives
 * the global-hotkey text-input guard, so a stale `true` would dead-lock every
 * digit/`:`/`q` hotkey from launch. This clears it on each tab entry, before
 * the entering screen's @ref IScreen::on_enter (which re-asserts its own text
 * focus when appropriate). The active-tab index is recomputed every call so the
 * caller can also use it to select the rendered tab child.
 *
 * @param screens The hosted screens, one or more per tab, looked up by
 * @ref IScreen::id; borrowed, not owned.
 * @param router The navigation state machine; @ref Router::active_tab selects
 * the screen to enter.
 * @param context The shared screen context; its @ref
 * ScreenContext::text_input_active is reset to `false` on every tab change.
 * @param entered_tab In/out: the tab whose screen last received `on_enter`
 * (`std::nullopt` before the first entry); updated to the now-active tab when a
 * change is handled.
 * @return The index into @p screens of the active screen, or `-1` when no
 * hosted screen matches the active tab.
 */
int sync_active_screen(const std::vector<std::unique_ptr<IScreen>>& screens,
                       Router& router,
                       ScreenContext& context,
                       std::optional<ScreenId>& entered_tab);

/**
 * @brief The result of dispatching one global hotkey through
 * @ref handle_global_key.
 */
struct GlobalKeyOutcome
{
    ///< `true` when the event was consumed by the global handler (so the active
    ///< screen beneath must not also see it); `false` when it should fall
    ///< through to the focused screen (e.g. a digit typed into a text field).
    bool handled = false;

    ///< `true` when `Ctrl-C` requested a clean quit; the caller invokes
    ///< `ScreenInteractive::Exit()` (kept out of this free function so it stays
    ///< headless-testable).
    bool exit_requested = false;
};

/**
 * @brief Dispatches one global hotkey: help overlay toggle, the focused
 * text-input guard, `Ctrl-C` quit, `Esc`/`q` back, and the digit/`:` tab
 * switch.
 *
 * Mirrors `App::run`'s `CatchEvent` handler exactly; the only behavioral split
 * is `Ctrl-C`, which sets @ref GlobalKeyOutcome::exit_requested instead of
 * calling `ScreenInteractive::Exit()` so this function needs no live screen.
 * While @p help_visible is set the overlay owns all input (only `Esc`/`F1`/`?`
 * close it); otherwise `F1` opens it. When @ref
 * ScreenContext::text_input_active is set every navigation key falls through
 * (`handled == false`) so it reaches the focused `Input` instead of switching
 * tabs (BUG-2 S4), while `F1` is still honored above the guard.
 *
 * @param event The event to dispatch.
 * @param router The navigation state machine; `navigate_tab`/`pop` are applied
 * for the recognized keys.
 * @param context The shared screen context; only @ref
 * ScreenContext::text_input_active is read.
 * @param help_visible In/out: the F1 help-overlay visibility, toggled by this
 * call.
 * @return The dispatch outcome (@ref GlobalKeyOutcome::handled and @ref
 * GlobalKeyOutcome::exit_requested).
 */
GlobalKeyOutcome handle_global_key(const ftxui::Event& event,
                                   Router& router,
                                   const ScreenContext& context,
                                   bool& help_visible);

}  // namespace url_shortener::tui
