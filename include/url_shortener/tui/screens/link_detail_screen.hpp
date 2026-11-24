#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/screen.hpp>

namespace url_shortener::tui
{

/**
 * @file link_detail_screen.hpp
 * @brief The Link detail / stats / preview screen (D4, subtask 06.0/13): the
 * detail of one link from `link.get`, the 7-day stats strip from `link.stats`,
 * and the `link.preview` result on demand, plus an inline edit form.
 *
 * The screen is reached from the Links tab (D3): Links sets
 * @ref ScreenContext::pending_slug and hands control to this screen, which
 * fetches `link.get` and `link.stats` concurrently through @ref AsyncCaller and
 * renders each pane independently (a failing stats call never blanks the link
 * fields). Because the D1 frame renders only tab screens and @ref App's global
 * Esc pops the router before any nested component sees it, the Link detail (the
 * D11 `link_detail` node) is rendered *inline* within its owning tab, exactly
 * like the Jobs screen's inline job detail (subtask 06.0/16); it is not
 * registered as a router/tab screen. The `e` edit opens the generated
 * `link.update` form inline (prefilled from the fetched JSON) rather than
 * pushing the `link_form` node, for the same reason. Every mutating submit
 * (edit / restore) routes through @ref run_mutation with the session read-only
 * flag (ADR 0003 rule 4, threat model T-116); the screen issues no command
 * through @ref ICommandClient directly (contract C8), tokenizes no JSON itself
 * (it reads results through `json_view`/the detail model, contract C9), and
 * sanitizes every server/stored string before display (threat model T-110).
 */

/**
 * @brief Creates the Link detail screen (D4).
 *
 * On @ref IScreen::on_enter the screen reads @ref ScreenContext::pending_slug
 * and submits `link.get` and `link.stats` (exactly two requests), rendering a
 * loading skeleton until they answer. Keys: `e` edit (inline `link.update`
 * form), `p` preview (inline `link.preview` result viewer), `s` toggle the
 * stats bucket and refetch, `r` refresh both, `u` restore a deleted link, and
 * `q`/`Esc` return to the caller via @p on_close. Every async call uses a
 * cancellable token + epoch so a stale/late response is dropped, and a
 * `not_found` `link.get` toasts and returns to the caller.
 *
 * @param context The screen context providing the async caller, session, router
 * and options; its @ref ScreenContext::caller is a reference owned by @ref App
 * that must outlive the returned screen (captured by reference).
 * @param commands The command catalog, used to resolve the `link.*` descriptors
 * the edit / restore actions need; moved in.
 * @param on_close Invoked on the UI thread when the operator leaves the detail
 * (`q`/`Esc`) or a `not_found` auto-pop fires, so the hosting screen can return
 * to the list; it must only set a deferred flag (it may tear down this screen),
 * never destroy the screen synchronously. Empty when the caller needs no hook.
 * @return An owning @ref IScreen for @ref ScreenId::link_detail; never null.
 */
std::unique_ptr<IScreen> make_link_detail_screen(
    ScreenContext& context,
    std::vector<CommandInfo> commands,
    std::function<void()> on_close = {});

}  // namespace url_shortener::tui
